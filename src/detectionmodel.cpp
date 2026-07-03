#include "detectionmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <QSet>
#include <QList>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>

static const char *kDataFileName = "01.Video_60sec_integrated.json";

static constexpr double kFrameDt    = 0.04; // 25 fps playback step (seconds)
static constexpr double kMinDepth   = 1.5;  // depth floor for a coasting vehicle
static constexpr int    kCoastMaxAge = 75;  // ~3 s a confirmed vehicle may coast unseen
static constexpr double kPedConf    = 0.20; // pedestrians read at low confidence

// The sim never overlaps two vehicles, so heavily overlapping boxes of any
// vehicle class are the same object (a car/truck class flip).
static bool isVehicleClass(const QString &c)
{
    // best.pt emits a generic "vehicle" class; COCO names kept in case the
    // perception model is swapped later.
    return c == QLatin1String("vehicle")
        || c == QLatin1String("car")     || c == QLatin1String("truck")
        || c == QLatin1String("bus")     || c == QLatin1String("motorcycle")
        || c == QLatin1String("bicycle");
}

// NMS / matching key: all vehicles collapse to one group, everything else
// stays its own class (a pedestrian beside a car must not be suppressed).
static QString groupKey(const QString &c)
{
    return isVehicleClass(c) ? QStringLiteral("vehicle") : c;
}

// IoU of two boxes given as center/size.
static double iouCxywh(double acx, double acy, double aw, double ah,
                       double bcx, double bcy, double bw, double bh)
{
    const double ax1 = acx - aw / 2, ay1 = acy - ah / 2, ax2 = acx + aw / 2, ay2 = acy + ah / 2;
    const double bx1 = bcx - bw / 2, by1 = bcy - bh / 2, bx2 = bcx + bw / 2, by2 = bcy + bh / 2;
    const double ix1 = qMax(ax1, bx1), iy1 = qMax(ay1, by1);
    const double ix2 = qMin(ax2, bx2), iy2 = qMin(ay2, by2);
    const double iw = qMax(0.0, ix2 - ix1), ih = qMax(0.0, iy2 - iy1);
    const double inter = iw * ih;
    const double uni = aw * ah + bw * bh - inter;
    return uni > 0.0 ? inter / uni : 0.0;
}

DetectionModel::DetectionModel(QObject *parent)
    : QAbstractListModel(parent)
{
    const QString path = resolveDataPath();
    if (path.isEmpty() || !loadFromFile(path))
        qWarning() << "DetectionModel: could not load detection data" << kDataFileName;

    // Source is 25 fps -> one frame every 40 ms.
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &DetectionModel::advanceFrame);
}

// Next to the executable first (CMake copies it there), else the source tree.
QString DetectionModel::resolveDataPath() const
{
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(kDataFileName);
    if (QFile::exists(beside))
        return beside;

#ifdef ADAS_SOURCE_DIR
    const QString dev = QDir(QStringLiteral(ADAS_SOURCE_DIR) + QStringLiteral("/data")).filePath(kDataFileName);
    if (QFile::exists(dev))
        return dev;
#endif

    return QString();
}

bool DetectionModel::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();

    // The file has no resolution field; default to the known 1280x720.
    const QJsonArray res = root.value("resolution").toArray();
    if (res.size() == 2) {
        m_sourceWidth = res.at(0).toInt(m_sourceWidth);
        m_sourceHeight = res.at(1).toInt(m_sourceHeight);
    }

    const QJsonArray frames = root.value("frames").toArray();
    m_frames.reserve(frames.size());
    for (const QJsonValue &frameVal : frames) {
        const QJsonObject frameObj = frameVal.toObject();
        FrameData frame;
        frame.timestampSec = frameObj.value("timestamp_sec").toDouble();

        const QJsonArray dets = frameObj.value("detections").toArray();
        frame.detections.reserve(dets.size());
        for (const QJsonValue &detVal : dets) {
            const QJsonObject detObj = detVal.toObject();
            const QJsonArray box = detObj.value("bbox_xyxy").toArray();
            if (box.size() != 4)
                continue;
            const double x1 = box.at(0).toDouble(), y1 = box.at(1).toDouble();
            const double x2 = box.at(2).toDouble(), y2 = box.at(3).toDouble();

            Detection d;
            d.className  = detObj.value("class_name").toString();
            d.confidence = detObj.value("confidence").toDouble();
            d.depth      = detObj.value("depth_m").toDouble(); // 0 if absent
            d.cx = (x1 + x2) / 2.0;
            d.cy = (y1 + y2) / 2.0;
            d.w  = x2 - x1;
            d.h  = y2 - y1;
            frame.detections.push_back(d);
        }

        const QJsonArray lanes = frameObj.value("lanes").toArray();
        frame.lanes.reserve(lanes.size());
        for (const QJsonValue &laneVal : lanes) {
            const QJsonObject laneObj = laneVal.toObject();
            LaneRaw lane;
            lane.index = laneObj.value("index").toInt();
            const QJsonArray pts = laneObj.value("points").toArray();
            lane.points.reserve(pts.size());
            for (const QJsonValue &ptVal : pts) {
                const QJsonArray p = ptVal.toArray();
                if (p.size() == 2)
                    lane.points.push_back(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
            }
            frame.lanes.push_back(lane);
        }

        m_frames.push_back(frame);
    }

    emit loaded();
    return !m_frames.isEmpty();
}

// ---------------------------------------------------------------------------
// Model interface (rows = confirmed tracks in m_rowIds order)
// ---------------------------------------------------------------------------
int DetectionModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rowIds.size();
}

QVariant DetectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rowIds.size())
        return QVariant();

    const Track t = m_tracks.value(m_rowIds.at(index.row()));
    switch (role) {
    case ClassNameRole:  return t.className;
    case CenterXRole:    return t.cx;
    case CenterYRole:    return t.cy;
    case BoxWidthRole:   return t.w;
    case BoxHeightRole:  return t.h;
    case ConfidenceRole: return t.confidence;
    case TrackIdRole:    return t.id;
    case HeadingRole:    return std::atan2(t.vy, t.vx);
    case DepthRole:      return t.depth;
    default:             return QVariant();
    }
}

QHash<int, QByteArray> DetectionModel::roleNames() const
{
    return {
        { ClassNameRole,  "className" },
        { CenterXRole,    "centerX" },
        { CenterYRole,    "centerY" },
        { BoxWidthRole,   "boxWidth" },
        { BoxHeightRole,  "boxHeight" },
        { ConfidenceRole, "confidence" },
        { TrackIdRole,    "trackId" },
        { HeadingRole,    "heading" },
        { DepthRole,      "depth" }
    };
}

double DetectionModel::timestampSec() const
{
    if (m_frames.isEmpty())
        return 0.0;
    return m_frames.at(m_frameIndex).timestampSec;
}

// ---------------------------------------------------------------------------
// Tracker
// ---------------------------------------------------------------------------
bool DetectionModel::isConfirmed(const Track &t) const
{
    // Pedestrians are sparse and weak, so they confirm faster (2 hits) or a
    // quick crossing never reaches the vehicle minHits.
    const int need = isVehicleClass(t.className) ? m_minHits : qMin(2, m_minHits);
    return t.hits >= need;
}

void DetectionModel::processFrame(int frame)
{
    if (m_frames.isEmpty())
        return;

    // 1. Confidence gate + size-vs-depth plausibility. Pedestrians get a lower
    //    gate (the model only reports them at conf ~0.25-0.46).
    QVector<Detection> dets;
    for (const Detection &d : m_frames.at(frame).detections) {
        const double gate = isVehicleClass(d.className) ? m_confThreshold : kPedConf;
        if (d.confidence >= gate && plausibleDetection(d))
            dets.push_back(d);
    }

    // 2. NMS: drop same-group boxes that heavily overlap a more confident one.
    std::sort(dets.begin(), dets.end(),
              [](const Detection &a, const Detection &b) { return a.confidence > b.confidence; });
    QVector<Detection> kept;
    QVector<bool> suppressed(dets.size(), false);
    for (int i = 0; i < dets.size(); ++i) {
        if (suppressed[i])
            continue;
        kept.push_back(dets[i]);
        for (int j = i + 1; j < dets.size(); ++j) {
            if (suppressed[j] || groupKey(dets[j].className) != groupKey(dets[i].className))
                continue;
            if (iouCxywh(dets[i].cx, dets[i].cy, dets[i].w, dets[i].h,
                         dets[j].cx, dets[j].cy, dets[j].w, dets[j].h) > m_nmsIou)
                suppressed[j] = true;
        }
    }

    // 3. Estimate ego (camera) motion as the median box displacement: when the
    //    car turns, the whole scene sweeps together. Only trusted when >= 3
    //    boxes agree, else left at zero.
    double egoDx = 0.0, egoDy = 0.0;
    {
        QVector<double> dxs, dys;
        const QList<int> tids = m_tracks.keys();
        for (int tid : tids) {
            const Track &t = m_tracks[tid];
            const double gate = m_gateScale * (t.w + t.h); // generous for this pass
            int best = -1;
            double bestDist = std::numeric_limits<double>::max();
            for (int d = 0; d < kept.size(); ++d) {
                if (kept[d].className != t.className)
                    continue;
                const double dist = std::hypot(kept[d].cx - t.cx, kept[d].cy - t.cy);
                if (dist < bestDist && dist <= gate) { bestDist = dist; best = d; }
            }
            if (best >= 0) {
                dxs.push_back(kept[best].cx - t.cx);
                dys.push_back(kept[best].cy - t.cy);
            }
        }
        if (dxs.size() >= 3) {
            std::sort(dxs.begin(), dxs.end());
            std::sort(dys.begin(), dys.end());
            egoDx = dxs.at(dxs.size() / 2);
            egoDy = dys.at(dys.size() / 2);
        }
    }

    // 4. Associate detections to tracks by center distance around each track's
    //    ego-predicted position (IoU is too brittle for small/distant boxes).
    struct Pair { int trackId; int detIdx; double dist; };
    QVector<Pair> pairs;
    const QList<int> trackIds = m_tracks.keys();
    for (int tid : trackIds) {
        const Track &t = m_tracks[tid];
        const double predCx = t.cx + egoDx, predCy = t.cy + egoDy;
        const double gate = m_gateScale * 0.5 * (t.w + t.h);
        for (int d = 0; d < kept.size(); ++d) {
            if (groupKey(kept[d].className) != groupKey(t.className))
                continue;
            const double dist = std::hypot(kept[d].cx - predCx, kept[d].cy - predCy);
            if (dist <= gate)
                pairs.push_back({ tid, d, dist });
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &a, const Pair &b) { return a.dist < b.dist; });

    QSet<int> matchedTracks;
    QVector<bool> matchedDet(kept.size(), false);
    const double a = m_emaAlpha;
    for (const Pair &p : pairs) {
        if (matchedTracks.contains(p.trackId) || matchedDet[p.detIdx])
            continue;
        matchedTracks.insert(p.trackId);
        matchedDet[p.detIdx] = true;

        Track &t = m_tracks[p.trackId];
        const Detection &d = kept[p.detIdx];
        const double predCx = t.cx + egoDx, predCy = t.cy + egoDy;
        // Velocity is the ego-removed (true) object motion -> heading.
        const double objDx = d.cx - predCx, objDy = d.cy - predCy;
        t.vx = a * objDx + (1 - a) * t.vx;
        t.vy = a * objDy + (1 - a) * t.vy;
        t.cx = a * d.cx + (1 - a) * predCx;
        t.cy = a * d.cy + (1 - a) * predCy;
        t.w  = a * d.w  + (1 - a) * t.w;
        t.h  = a * d.h  + (1 - a) * t.h;
        // EMA the depth when present, else keep the last known distance
        // (the model occasionally drops depth_m).
        if (d.depth > 0.0)
            t.depth = (t.depth > 0.0) ? a * d.depth + (1 - a) * t.depth : d.depth;
        t.confidence = d.confidence;
        t.hits++;
        t.missed = 0;
    }

    // 4b. Recapture: a leftover detection overlapping an existing (often
    //     coasted) vehicle track is the same car re-detected after a gap, so
    //     snap that track onto it instead of spawning a clone.
    for (int d = 0; d < kept.size(); ++d) {
        if (matchedDet[d] || !isVehicleClass(kept[d].className))
            continue;
        int best = -1;
        double bestDist = std::numeric_limits<double>::max();
        for (int tid : trackIds) {
            if (matchedTracks.contains(tid))
                continue;
            const Track &t = m_tracks[tid];
            if (!isVehicleClass(t.className))
                continue;
            const double tol = 0.7 * (t.w + t.h);   // overlapping ~ same object
            const double dist = std::hypot(kept[d].cx - t.cx, kept[d].cy - t.cy);
            if (dist < bestDist && dist <= tol) { bestDist = dist; best = tid; }
        }
        if (best < 0)
            continue;
        matchedTracks.insert(best);
        matchedDet[d] = true;
        Track &t = m_tracks[best];
        const Detection &dd = kept[d];
        t.cx = dd.cx; t.cy = dd.cy; t.w = dd.w; t.h = dd.h;
        if (dd.depth > 0.0) t.depth = dd.depth;
        t.vx = t.vy = 0.0;
        t.confidence = dd.confidence;
        t.hits++;
        t.missed = 0;
    }

    // 5. Unmatched tracks coast. A confirmed vehicle with a depth is held as
    //    world-static: the ego closes the distance each frame (depth shrinks,
    //    box grows) and it is only removed once it leaves the camera frame or
    //    has been unseen too long. Everything else keeps the plain velocity
    //    coast + age-out.
    const double fx = m_sourceWidth / (2.0 * std::tan(qDegreesToRadians(m_cameraFovDeg) / 2.0));
    const double egoFwdM = (m_egoSpeedKmh / 3.6) * kFrameDt;
    QVector<int> toRemove;
    for (int tid : trackIds) {
        if (matchedTracks.contains(tid))
            continue;
        Track &t = m_tracks[tid];
        t.missed++;
        const bool persist = isVehicleClass(t.className) && t.depth > 0.0 && t.hits >= m_minHits;
        if (persist) {
            const double lateralM = (t.cx - m_sourceWidth * 0.5) / fx * t.depth;
            double newDepth = t.depth - egoFwdM;
            if (newDepth < kMinDepth) newDepth = kMinDepth;
            const double grow = t.depth / newDepth;          // closer -> larger on screen
            t.cx = m_sourceWidth * 0.5 + (lateralM / newDepth) * fx;
            t.cy = m_sourceHeight * 0.5 + (t.cy - m_sourceHeight * 0.5) * grow;
            t.w *= grow; t.h *= grow;
            t.depth = newDepth;
            const bool offFrame = (t.cx - t.w * 0.5 > m_sourceWidth)
                               || (t.cx + t.w * 0.5 < 0.0)
                               || (t.cy - t.h * 0.5 > m_sourceHeight);
            if (offFrame || t.missed > kCoastMaxAge)
                toRemove.push_back(tid);
        } else {
            t.cx += egoDx + t.vx;
            t.cy += egoDy + t.vy;
            if (t.missed > m_maxAge)
                toRemove.push_back(tid);
        }
    }
    for (int tid : toRemove)
        m_tracks.remove(tid);

    // 6. Unmatched detections spawn new (tentative) tracks.
    for (int d = 0; d < kept.size(); ++d) {
        if (matchedDet[d])
            continue;
        Track t;
        t.id = m_nextTrackId++;
        t.className = kept[d].className;
        t.cx = kept[d].cx; t.cy = kept[d].cy;
        t.w = kept[d].w;   t.h = kept[d].h;
        t.depth = kept[d].depth;
        t.confidence = kept[d].confidence;
        t.hits = 1; t.missed = 0;
        m_tracks.insert(t.id, t);
    }

    syncRows();
}

// Expected real height (metres) per class, for the size-vs-depth check.
static double vehicleHeightM(const QString &c)
{
    if (c == QLatin1String("truck"))      return 3.5;
    if (c == QLatin1String("bus"))        return 3.2;
    if (c == QLatin1String("bicycle"))    return 1.6;
    if (c == QLatin1String("motorcycle")) return 1.5;
    return 1.7; // vehicle / car
}

bool DetectionModel::plausibleDetection(const Detection &d) const
{
    if (!isVehicleClass(d.className) || d.depth <= 0.0 || d.h <= 0.0)
        return true; // only vehicles with a depth reading are checked
    // A real vehicle at depth d subtends about focal * realHeight / d pixels.
    const double fy = m_sourceWidth / (2.0 * std::tan(qDegreesToRadians(m_cameraFovDeg) / 2.0));
    const double expectedH = fy * vehicleHeightM(d.className) / d.depth;
    if (expectedH <= 0.0)
        return true;
    const double ratio = d.h / expectedH;
    return ratio >= 0.30 && ratio <= 3.50; // wide bounds: generic class + pose variation
}

// Pinhole lateral gate: metres-off-centre from image x + depth, kept only
// inside the three-lane band.
bool DetectionModel::withinLaneBand(const Track &t) const
{
    if (!isVehicleClass(t.className))
        return true; // pedestrians / signs aren't lane-gated

    if (t.depth > 0.0) {
        const double fx = m_sourceWidth / (2.0 * std::tan(qDegreesToRadians(m_cameraFovDeg) / 2.0));
        const double lateralM = (t.cx - m_sourceWidth * 0.5) / fx * t.depth;
        return std::fabs(lateralM) <= m_laneHalfSpan;
    }
    // No depth: best-effort, drop only the far frame edges.
    const double nx = t.cx / double(m_sourceWidth);
    return std::fabs(nx - 0.5) < 0.45;
}

// Diff the visible set against current rows and emit granular model signals so
// QML delegates persist instead of being rebuilt each frame.
void DetectionModel::syncRows()
{
    QVector<int> confirmed;
    for (auto it = m_tracks.constBegin(); it != m_tracks.constEnd(); ++it) {
        const Track &t = it.value();
        if (isConfirmed(t) && withinLaneBand(t))
            confirmed.push_back(it.key());
    }

    // De-duplicate overlapping vehicles: best-established tracks first, then
    // drop any later vehicle that coincides with one already kept. "Coincides"
    // needs BOTH the same screen position and a similar depth — a near and a
    // far car can share a screen column and must both stay.
    std::sort(confirmed.begin(), confirmed.end(),
              [this](int a, int b) { return m_tracks[a].hits > m_tracks[b].hits; });
    QVector<int> visible;
    for (int id : confirmed) {
        const Track &t = m_tracks[id];
        bool dup = false;
        if (isVehicleClass(t.className)) {
            for (int keptId : visible) {
                const Track &s = m_tracks[keptId];
                if (!isVehicleClass(s.className))
                    continue;
                const bool sameDepth = (t.depth > 0.0 && s.depth > 0.0)
                    ? std::fabs(t.depth - s.depth) < 0.30 * qMax(t.depth, s.depth)
                    : true;
                const double tol = 0.18 * (t.w + t.h); // tight: only near-coincident boxes
                if (sameDepth && std::hypot(t.cx - s.cx, t.cy - s.cy) < tol) { dup = true; break; }
            }
        }
        if (!dup)
            visible.push_back(id);
    }

    // Stable row order by id so existing delegates keep their slot.
    std::sort(visible.begin(), visible.end());

    // Removals (back to front so row indices stay valid).
    for (int row = m_rowIds.size() - 1; row >= 0; --row) {
        if (!visible.contains(m_rowIds.at(row))) {
            beginRemoveRows(QModelIndex(), row, row);
            m_rowIds.removeAt(row);
            endRemoveRows();
        }
    }

    // Insertions appended in stable id order.
    for (int id : visible) {
        if (!m_rowIds.contains(id)) {
            const int row = m_rowIds.size();
            beginInsertRows(QModelIndex(), row, row);
            m_rowIds.append(id);
            endInsertRows();
        }
    }

    // Survivors moved this frame -> refresh their data.
    if (!m_rowIds.isEmpty())
        emit dataChanged(index(0), index(m_rowIds.size() - 1));
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------
void DetectionModel::advanceFrame()
{
    if (m_frames.isEmpty())
        return;
    const int next = m_frameIndex + 1;
    if (next >= m_frames.size()) {
        // Scenario over: stop and let the dashboard drop back to Park.
        pause();
        emit scenarioEnded();
    } else {
        gotoFrame(next, true);
    }
}

void DetectionModel::gotoFrame(int frame, bool continuous)
{
    if (m_frames.isEmpty())
        return;
    frame = qBound(0, frame, m_frames.size() - 1);

    if (!continuous) {
        // Discontinuous jump: drop all tracker state and clear the model.
        if (!m_rowIds.isEmpty()) {
            beginResetModel();
            m_rowIds.clear();
            m_tracks.clear();
            endResetModel();
        } else {
            m_tracks.clear();
        }
    }

    m_frameIndex = frame;
    processFrame(frame);
    emit frameIndexChanged();
}

void DetectionModel::play()
{
    if (m_frames.isEmpty() || m_timer.isActive())
        return;
    m_timer.start();
    emit runningChanged();
}

void DetectionModel::pause()
{
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    emit runningChanged();
}

void DetectionModel::seek(int frame)
{
    gotoFrame(frame, false);
}

// ---------------------------------------------------------------------------
// Tunable parameters
// ---------------------------------------------------------------------------
void DetectionModel::setConfThreshold(double v)
{
    if (qFuzzyCompare(m_confThreshold, v)) return;
    m_confThreshold = v; emit paramsChanged();
}
void DetectionModel::setIouMatch(double v)
{
    if (qFuzzyCompare(m_iouMatch, v)) return;
    m_iouMatch = v; emit paramsChanged();
}
void DetectionModel::setMaxAge(int v)
{
    if (m_maxAge == v) return;
    m_maxAge = v; emit paramsChanged();
}
void DetectionModel::setMinHits(int v)
{
    if (m_minHits == v) return;
    m_minHits = v; emit paramsChanged();
}
void DetectionModel::setEmaAlpha(double v)
{
    if (qFuzzyCompare(m_emaAlpha, v)) return;
    m_emaAlpha = v; emit paramsChanged();
}
void DetectionModel::setGateScale(double v)
{
    if (qFuzzyCompare(m_gateScale, v)) return;
    m_gateScale = v; emit paramsChanged();
}
