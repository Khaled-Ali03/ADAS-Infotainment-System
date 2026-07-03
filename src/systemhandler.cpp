#include "systemhandler.h"
#include "detectionmodel.h"

#include <QLocale>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

const TelemetryFrame SystemHandler::s_idle = TelemetryFrame{};

SystemHandler::SystemHandler(DetectionModel *detection, QObject *parent)
    : QObject(parent)
    , m_detection(detection)
{
    // Tick the displayed clock once a minute.
    m_clockTimer.setInterval(60 * 1000);
    connect(&m_clockTimer, &QTimer::timeout, this, &SystemHandler::clockChanged);
    m_clockTimer.start();

    loadTelemetry();

    // Stay in lockstep with the detection playback clock.
    if (m_detection) {
        connect(m_detection, &DetectionModel::frameIndexChanged,
                this, &SystemHandler::syncToDetectionFrame);
        connect(m_detection, &DetectionModel::loaded,
                this, &SystemHandler::syncToDetectionFrame);
        // When the captured drive runs out, drop back to Park.
        connect(m_detection, &DetectionModel::scenarioEnded,
                this, [this]() { setGear(QStringLiteral("P")); });
    }
}

QString SystemHandler::resolveTelemetryPath() const
{
    // Next to the binary first (CMake copies it there), else the source tree.
    const QString beside = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/ego_telemetry.json");
    if (QFile::exists(beside))
        return beside;

#ifdef ADAS_SOURCE_DIR
    const QString src = QStringLiteral(ADAS_SOURCE_DIR) + QStringLiteral("/data/ego_telemetry.json");
    if (QFile::exists(src))
        return src;
#endif
    return beside;
}

bool SystemHandler::loadTelemetry()
{
    QFile f(resolveTelemetryPath());
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("SystemHandler: could not open ego_telemetry.json");
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    const QJsonArray frames = doc.object().value(QStringLiteral("frames")).toArray();

    m_frames.clear();
    m_frames.reserve(frames.size());
    for (const QJsonValue &v : frames) {
        const QJsonObject o = v.toObject();
        TelemetryFrame tf;
        tf.steer        = o.value(QStringLiteral("steer")).toDouble();
        tf.throttle     = o.value(QStringLiteral("throttle")).toDouble();
        tf.brake        = o.value(QStringLiteral("brake")).toDouble();
        tf.handBrake    = o.value(QStringLiteral("hand_brake")).toBool();
        tf.reverse      = o.value(QStringLiteral("reverse")).toBool();
        tf.speedKmh     = o.value(QStringLiteral("speed_kmh")).toDouble();
        tf.yawRate      = o.value(QStringLiteral("yaw_rate")).toDouble();
        tf.heading      = o.value(QStringLiteral("heading")).toDouble();
        tf.pitch        = o.value(QStringLiteral("pitch")).toDouble();
        tf.roll         = o.value(QStringLiteral("roll")).toDouble();
        tf.speedLimitKmh = qRound(o.value(QStringLiteral("speed_limit_kmh")).toDouble());
        tf.trafficLight = o.value(QStringLiteral("traffic_light")).toString(QStringLiteral("none"));
        const QJsonObject w = o.value(QStringLiteral("weather")).toObject();
        tf.precipitation = w.value(QStringLiteral("precipitation")).toDouble();
        tf.fogDensity    = w.value(QStringLiteral("fog_density")).toDouble();
        tf.wetness       = w.value(QStringLiteral("wetness")).toDouble();
        m_frames.append(tf);
    }
    return !m_frames.isEmpty();
}

const TelemetryFrame &SystemHandler::cur() const
{
    if (m_frames.isEmpty() || m_telIndex < 0 || m_telIndex >= m_frames.size())
        return s_idle;
    return m_frames[m_telIndex];
}

void SystemHandler::syncToDetectionFrame()
{
    if (!m_detection || m_frames.isEmpty())
        return;

    // The detection clip (1500 frames) and the telemetry capture (~1906) are
    // the same drive at different rates; map proportionally by index.
    const int detCount = m_detection->frameCount();
    int idx;
    if (detCount > 1) {
        const double t = double(m_detection->frameIndex()) / double(detCount - 1);
        idx = qRound(t * (m_frames.size() - 1));
    } else {
        idx = m_detection->frameIndex();
    }
    idx = qBound(0, idx, m_frames.size() - 1);
    if (idx == m_telIndex)
        return;
    m_telIndex = idx;

    // Feed ego speed to the tracker so it can coast undetected vehicles.
    m_detection->setEgoSpeedKmh(driving() ? m_frames[idx].speedKmh : 0.0);

    // De-noise steering with a moving average. The capture is a sawtooth (the
    // driver repeatedly tightens then eases through a curve), so any filter
    // that follows the latest sample keeps wiggling; averaging over ~one
    // period gives the steady trend of the turn.
    m_steerHist.append(m_frames[idx].steer);
    while (m_steerHist.size() > kSteerWindow)
        m_steerHist.removeFirst();
    double sum = 0.0;
    for (double v : m_steerHist)
        sum += v;
    m_steerSmoothed = sum / m_steerHist.size();

    clampCruiseToLimit();
    emit telemetryChanged();
}

void SystemHandler::clampCruiseToLimit()
{
    // Cruise never exceeds the posted limit; follow the limit down if needed.
    const int lim = cur().speedLimitKmh;
    if (lim > 0 && m_cruiseKmh > lim) {
        m_cruiseKmh = lim;
        emit cruiseChanged();
    }
}

QString SystemHandler::clockText() const
{
    return QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("hh:mm"));
}

int SystemHandler::turnSignal() const
{
    if (!driving())
        return 0;
    const double s = cur().steer;
    if (s < -0.12) return -1;
    if (s >  0.12) return  1;
    return 0;
}

void SystemHandler::setGear(const QString &gear)
{
    const QString g = gear.toUpper();
    if (g != QStringLiteral("P") && g != QStringLiteral("R")
        && g != QStringLiteral("N") && g != QStringLiteral("D"))
        return;
    if (m_gear == g)
        return;

    m_gear = g;
    if (g == QStringLiteral("P")) {
        // Reset the steering filter so re-entering drive starts straight.
        m_steerSmoothed = 0.0;
        m_steerHist.clear();
    }
    emit gearChanged();
    // driving() flips most telemetry getters between live values and idle.
    emit telemetryChanged();
}

void SystemHandler::setAmbientTempC(double t)
{
    if (qFuzzyCompare(m_ambientTempC, t))
        return;
    m_ambientTempC = t;
    emit ambientTempChanged();
}

void SystemHandler::setBatteryPercent(int pct)
{
    pct = qBound(0, pct, 100);
    if (m_batteryPercent == pct)
        return;
    m_batteryPercent = pct;
    emit batteryChanged();
}

void SystemHandler::setCharging(bool c)
{
    if (m_charging == c)
        return;
    m_charging = c;
    emit chargingChanged();
}

void SystemHandler::setLocked(bool v)
{
    if (m_locked == v) return;
    m_locked = v;
    emit bodyChanged();
}

void SystemHandler::setFrunkOpen(bool v)
{
    if (m_frunkOpen == v) return;
    m_frunkOpen = v;
    emit bodyChanged();
}

void SystemHandler::setTrunkOpen(bool v)
{
    if (m_trunkOpen == v) return;
    m_trunkOpen = v;
    emit bodyChanged();
}

void SystemHandler::setDoorsOpen(bool v)
{
    if (m_doorsOpen == v) return;
    m_doorsOpen = v;
    emit bodyChanged();
}

void SystemHandler::setUseMph(bool v)
{
    if (m_useMph == v) return;
    m_useMph = v;
    emit unitChanged();
    emit telemetryChanged();   // speedDisplay depends on the unit
    emit cruiseChanged();      // cruiseDisplay too
}

void SystemHandler::setCruiseEnabled(bool v)
{
    if (m_cruiseEnabled == v) return;
    m_cruiseEnabled = v;
    if (v) clampCruiseToLimit();
    emit cruiseChanged();
}

void SystemHandler::cruiseUp()
{
    const int lim = cur().speedLimitKmh;
    const double ceil = lim > 0 ? double(lim) : 240.0;
    m_cruiseKmh = qBound(0.0, m_cruiseKmh + (m_useMph ? 2.0 / 0.621371 : 5.0), ceil);
    emit cruiseChanged();
}

void SystemHandler::cruiseDown()
{
    m_cruiseKmh = qBound(0.0, m_cruiseKmh - (m_useMph ? 2.0 / 0.621371 : 5.0), 240.0);
    emit cruiseChanged();
}
