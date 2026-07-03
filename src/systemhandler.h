#ifndef SYSTEMHANDLER_H
#define SYSTEMHANDLER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <QVector>

class DetectionModel;

// One frame of CARLA ego telemetry (see ego_telemetry.json).
struct TelemetryFrame {
    double steer = 0.0;          // -1 (full left) .. +1 (full right)
    double throttle = 0.0;       // 0..1
    double brake = 0.0;          // 0..1
    bool   handBrake = false;
    bool   reverse = false;
    double speedKmh = 0.0;
    double yawRate = 0.0;        // deg/s
    double heading = 0.0;        // compass yaw, degrees
    double pitch = 0.0;
    double roll = 0.0;
    int    speedLimitKmh = 0;
    QString trafficLight = QStringLiteral("none"); // Red/Yellow/Green/Off/none
    double precipitation = 0.0;
    double fogDensity = 0.0;
    double wetness = 0.0;
};

// Ego-vehicle state. Real signals (speed, steering, signs, traffic light,
// weather) are played back from ego_telemetry.json, frame-synced to
// DetectionModel so everything shares one clock. Signals the capture doesn't
// have (battery, cabin temp, body state) stay simulated, and gear is a manual
// P/R/N/D override. Setters only emit when a value actually changes.
class SystemHandler : public QObject
{
    Q_OBJECT
    // --- manual / simulated ---
    Q_PROPERTY(QString gear READ gear WRITE setGear NOTIFY gearChanged)
    Q_PROPERTY(bool driving READ driving NOTIFY gearChanged)
    Q_PROPERTY(double ambientTempC READ ambientTempC WRITE setAmbientTempC NOTIFY ambientTempChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent WRITE setBatteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(bool charging READ charging WRITE setCharging NOTIFY chargingChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged)

    // --- body state (simulated) ---
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY bodyChanged)
    Q_PROPERTY(bool frunkOpen READ frunkOpen WRITE setFrunkOpen NOTIFY bodyChanged)
    Q_PROPERTY(bool trunkOpen READ trunkOpen WRITE setTrunkOpen NOTIFY bodyChanged)
    Q_PROPERTY(bool doorsOpen READ doorsOpen WRITE setDoorsOpen NOTIFY bodyChanged)

    // --- speed unit + cruise control ---
    Q_PROPERTY(bool useMph READ useMph WRITE setUseMph NOTIFY unitChanged)
    Q_PROPERTY(QString speedUnitText READ speedUnitText NOTIFY unitChanged)
    Q_PROPERTY(int speedDisplay READ speedDisplay NOTIFY telemetryChanged)
    Q_PROPERTY(int cruiseDisplay READ cruiseDisplay NOTIFY cruiseChanged)
    Q_PROPERTY(bool cruiseEnabled READ cruiseEnabled WRITE setCruiseEnabled NOTIFY cruiseChanged)

    // --- real, from ego_telemetry.json (valid while driving) ---
    Q_PROPERTY(int speedKmh READ speedKmh NOTIFY telemetryChanged)
    Q_PROPERTY(double steer READ steer NOTIFY telemetryChanged)
    // De-noised steering for the wheel/car visuals (the raw capture jitters).
    Q_PROPERTY(double steerSmoothed READ steerSmoothed NOTIFY telemetryChanged)
    Q_PROPERTY(double throttle READ throttle NOTIFY telemetryChanged)
    Q_PROPERTY(double brake READ brake NOTIFY telemetryChanged)
    Q_PROPERTY(bool handBrake READ handBrake NOTIFY telemetryChanged)
    Q_PROPERTY(bool reverse READ reverse NOTIFY telemetryChanged)
    Q_PROPERTY(double yawRate READ yawRate NOTIFY telemetryChanged)
    Q_PROPERTY(double heading READ heading NOTIFY telemetryChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY telemetryChanged)
    Q_PROPERTY(double roll READ roll NOTIFY telemetryChanged)
    Q_PROPERTY(int speedLimitKmh READ speedLimitKmh NOTIFY telemetryChanged)
    Q_PROPERTY(QString trafficLight READ trafficLight NOTIFY telemetryChanged)
    Q_PROPERTY(double precipitation READ precipitation NOTIFY telemetryChanged)
    Q_PROPERTY(double fogDensity READ fogDensity NOTIFY telemetryChanged)
    Q_PROPERTY(double wetness READ wetness NOTIFY telemetryChanged)
    // Turn-signal intent derived from steering (no blinker in the capture).
    Q_PROPERTY(int turnSignal READ turnSignal NOTIFY telemetryChanged) // -1 left, 0 off, +1 right

public:
    explicit SystemHandler(DetectionModel *detection = nullptr, QObject *parent = nullptr);

    QString gear() const { return m_gear; }
    bool driving() const { return m_gear != QStringLiteral("P"); }
    double ambientTempC() const { return m_ambientTempC; }
    int batteryPercent() const { return m_batteryPercent; }
    bool charging() const { return m_charging; }
    QString clockText() const;

    int speedKmh() const { return driving() ? qRound(cur().speedKmh) : 0; }
    double steer() const { return driving() ? cur().steer : 0.0; }
    double steerSmoothed() const { return driving() ? m_steerSmoothed : 0.0; }
    double throttle() const { return driving() ? cur().throttle : 0.0; }
    double brake() const { return driving() ? cur().brake : 0.0; }
    bool handBrake() const { return driving() ? cur().handBrake : (m_gear == QStringLiteral("P")); }
    bool reverse() const { return m_gear == QStringLiteral("R"); }
    double yawRate() const { return driving() ? cur().yawRate : 0.0; }
    double heading() const { return cur().heading; }
    double pitch() const { return driving() ? cur().pitch : 0.0; }
    double roll() const { return driving() ? cur().roll : 0.0; }
    int speedLimitKmh() const { return cur().speedLimitKmh; }
    QString trafficLight() const { return driving() ? cur().trafficLight : QStringLiteral("none"); }
    double precipitation() const { return cur().precipitation; }
    double fogDensity() const { return cur().fogDensity; }
    double wetness() const { return cur().wetness; }
    int turnSignal() const;

    bool locked() const { return m_locked; }
    bool frunkOpen() const { return m_frunkOpen; }
    bool trunkOpen() const { return m_trunkOpen; }
    bool doorsOpen() const { return m_doorsOpen; }

    bool useMph() const { return m_useMph; }
    QString speedUnitText() const { return m_useMph ? QStringLiteral("mph") : QStringLiteral("km/h"); }
    int speedDisplay() const { return m_useMph ? qRound(speedKmh() * 0.621371) : speedKmh(); }
    int cruiseDisplay() const { return qRound(m_useMph ? m_cruiseKmh * 0.621371 : m_cruiseKmh); }
    bool cruiseEnabled() const { return m_cruiseEnabled; }

    void setGear(const QString &gear);
    void setAmbientTempC(double t);
    void setBatteryPercent(int pct);
    void setCharging(bool c);
    void setLocked(bool v);
    void setFrunkOpen(bool v);
    void setTrunkOpen(bool v);
    void setDoorsOpen(bool v);
    void setUseMph(bool v);
    void setCruiseEnabled(bool v);

public slots:
    void toggleLock()  { setLocked(!m_locked); }
    void toggleFrunk() { setFrunkOpen(!m_frunkOpen); }
    void toggleTrunk() { setTrunkOpen(!m_trunkOpen); }
    void toggleDoors() { setDoorsOpen(!m_doorsOpen); }
    void toggleCruise() { setCruiseEnabled(!m_cruiseEnabled); }
    void cruiseUp();    // +2 mph / +5 km/h, never above the posted limit
    void cruiseDown();

signals:
    void gearChanged();
    void ambientTempChanged();
    void batteryChanged();
    void chargingChanged();
    void clockChanged();
    void telemetryChanged();
    void bodyChanged();
    void unitChanged();
    void cruiseChanged();

private:
    bool loadTelemetry();
    QString resolveTelemetryPath() const;
    const TelemetryFrame &cur() const;          // telemetry at the synced frame
    void syncToDetectionFrame();                // map detection index -> telemetry
    void clampCruiseToLimit();

    QString m_gear = QStringLiteral("P"); // start parked
    double m_ambientTempC = 24.0;
    int m_batteryPercent = 76;
    bool m_charging = false;

    bool m_locked = true;
    bool m_frunkOpen = false;
    bool m_trunkOpen = false;
    bool m_doorsOpen = false;

    bool m_useMph = false;       // default metric (telemetry is km/h)
    double m_cruiseKmh = 60.0;   // cruise target, stored in km/h
    bool m_cruiseEnabled = false;
    double m_steerSmoothed = 0.0;
    QVector<double> m_steerHist; // moving-average window over recent steer samples
    static constexpr int kSteerWindow = 20; // ~one sawtooth period of the capture

    QVector<TelemetryFrame> m_frames;
    int m_telIndex = 0;
    DetectionModel *m_detection = nullptr;
    static const TelemetryFrame s_idle;  // fallback when no data / parked

    QTimer m_clockTimer;
};

#endif // SYSTEMHANDLER_H
