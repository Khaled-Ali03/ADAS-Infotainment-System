#include "lanemodel.h"
#include "detectionmodel.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

// CULane native output resolution; points are normalised by this so they share
// the Left Screen's [0,1] space with the detections.
static constexpr double kLaneW = 1640.0;
static constexpr double kLaneH = 590.0;

static constexpr int    kMinPts    = 6;    // raw points needed to attempt a fit
static constexpr int    kSamples   = 16;   // points emitted per rendered lane
static constexpr double kMinYSpan  = 0.18; // reject near-horizon slivers

// Rigid road-model geometry / smoothing (see buildRows).
static constexpr double kLaneYTop  = 0.33; // top of the drawn road band (normalised y)
static constexpr double kLaneYBot  = 1.00; // bottom, nearest the ego car
static constexpr double kPerspTop  = 0.38; // lateral offset scale at the top
static constexpr double kPerspBot  = 1.00; // ... and at the bottom
static constexpr double kRoadAlpha = 0.20; // smoothing for curvature / centre
static constexpr int    kMaxRoadMissed = 25; // frames the road model may coast (~1 s)
static constexpr double kEgoAnchorX = 0.50; // the ego car sits at screen centre
static constexpr double kAdjMargin  = 0.04; // clearance beyond the ego boundary to count as adjacent

static double evalLane(double a, double b, double c, double y) { return a * y * y + b * y + c; }

static double medianOf(QVector<double> v)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v.at(v.size() / 2);
}

LaneModel::LaneModel(DetectionModel *detection, QObject *parent)
    : QAbstractListModel(parent), m_detection(detection)
{
    if (m_detection) {
        connect(m_detection, &DetectionModel::frameIndexChanged, this, &LaneModel::rebuild);
        connect(m_detection, &DetectionModel::loaded, this, &LaneModel::rebuild);
    }
    rebuild();
}

// ---------------------------------------------------------------------------
// Model interface
// ---------------------------------------------------------------------------
int LaneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant LaneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case PointsRole:   return r.points;
    case LaneTypeRole: return r.laneType;
    case SideRole:     return r.side;
    default:           return QVariant();
    }
}

QHash<int, QByteArray> LaneModel::roleNames() const
{
    return {
        { PointsRole,   "points" },
        { LaneTypeRole, "laneType" },
        { SideRole,     "side" }
    };
}

// ---------------------------------------------------------------------------
// Per-frame pipeline
// ---------------------------------------------------------------------------
void LaneModel::rebuild()
{
    if (!m_detection)
        return;
    const QVector<FrameData> &frames = m_detection->frames();
    if (frames.isEmpty())
        return;

    const int frame = qBound(0, m_detection->frameIndex(), frames.size() - 1);
    const bool discontinuous = (frame != m_lastFrame + 1);
    m_lastFrame = frame;

    // 1. Fit each raw lane to a quadratic in normalised coords; keep good fits.
    QVector<Candidate> cands;
    for (const LaneRaw &lane : frames.at(frame).lanes) {
        if (lane.points.size() < kMinPts)
            continue;
        QVector<QPointF> norm;
        norm.reserve(lane.points.size());
        for (const QPointF &p : lane.points)
            norm.push_back(QPointF(p.x() / kLaneW, p.y() / kLaneH));
        Candidate cand;
        if (fitQuadratic(norm, cand))
            cands.push_back(cand);
    }

    // 2. Track/coast/confirm, then 3. classify + sample into renderable rows.
    updateTracks(cands, discontinuous);
    buildRows();
}

// Least-squares fit x = a*y^2 + b*y + c, with an RMS-residual reject so noisy
// or phantom polylines are dropped.
bool LaneModel::fitQuadratic(const QVector<QPointF> &p, Candidate &out) const
{
    const int n = p.size();
    if (n < kMinPts)
        return false;

    double yMin = 1.0, yMax = 0.0;
    double S0 = n, S1 = 0, S2 = 0, S3 = 0, S4 = 0, T0 = 0, T1 = 0, T2 = 0;
    for (const QPointF &q : p) {
        const double y = q.y(), x = q.x();
        const double y2 = y * y;
        S1 += y;  S2 += y2;  S3 += y2 * y;  S4 += y2 * y2;
        T0 += x;  T1 += x * y;  T2 += x * y2;
        yMin = qMin(yMin, y);  yMax = qMax(yMax, y);
    }

    // Too little vertical span to fit x = f(y) stably.
    if (yMax - yMin < kMinYSpan)
        return false;

    // Solve the 3x3 normal equations for [c, b, a] via Cramer's rule.
    const double det =
        S0 * (S2 * S4 - S3 * S3) - S1 * (S1 * S4 - S3 * S2) + S2 * (S1 * S3 - S2 * S2);
    if (std::fabs(det) < 1e-12)
        return false;

    const double c = (T0 * (S2 * S4 - S3 * S3) - S1 * (T1 * S4 - S3 * T2) + S2 * (T1 * S3 - S2 * T2)) / det;
    const double b = (S0 * (T1 * S4 - T2 * S3) - T0 * (S1 * S4 - S3 * S2) + S2 * (S1 * T2 - T1 * S2)) / det;
    const double a = (S0 * (S2 * T2 - T1 * S3) - S1 * (S1 * T2 - T1 * S2) + T0 * (S1 * S3 - S2 * S2)) / det;

    double sse = 0.0;
    for (const QPointF &q : p) {
        const double y = q.y();
        const double r = q.x() - (a * y * y + b * y + c);
        sse += r * r;
    }
    if (std::sqrt(sse / n) > m_maxResidual)
        return false;

    out.a = a; out.b = b; out.c = c;
    out.nearX = evalLane(a, b, c, yMax); // x at the lane's lowest observed point
    out.yTop = yMin; out.yBot = yMax;
    return true;
}

void LaneModel::updateTracks(const QVector<Candidate> &cands, bool discontinuous)
{
    if (discontinuous)
        m_tracks.clear();

    // Greedy nearest match by bottom-x (road position); robust to index churn.
    struct Pair { int track; int cand; double d; };
    QVector<Pair> pairs;
    for (int t = 0; t < m_tracks.size(); ++t)
        for (int c = 0; c < cands.size(); ++c) {
            const double d = std::fabs(cands[c].nearX - m_tracks[t].nearX);
            if (d <= m_nearGate)
                pairs.push_back({ t, c, d });
        }
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &x, const Pair &y) { return x.d < y.d; });

    QVector<bool> tMatched(m_tracks.size(), false), cMatched(cands.size(), false);
    const double k = m_emaAlpha;
    for (const Pair &p : pairs) {
        if (tMatched[p.track] || cMatched[p.cand])
            continue;
        tMatched[p.track] = cMatched[p.cand] = true;
        LaneTrack &t = m_tracks[p.track];
        const Candidate &c = cands[p.cand];
        t.a = k * c.a + (1 - k) * t.a;
        t.b = k * c.b + (1 - k) * t.b;
        t.c = k * c.c + (1 - k) * t.c;
        t.nearX = k * c.nearX + (1 - k) * t.nearX;
        t.yTop = k * c.yTop + (1 - k) * t.yTop;
        t.yBot = k * c.yBot + (1 - k) * t.yBot;
        t.side = t.nearX < 0.5 ? -1 : 1;
        t.hits++;
        t.missed = 0;
    }

    // Unmatched tracks coast (hold geometry); drop once too stale.
    for (int t = m_tracks.size() - 1; t >= 0; --t) {
        if (tMatched[t])
            continue;
        m_tracks[t].missed++;
        if (m_tracks[t].missed > m_maxAge)
            m_tracks.remove(t);
    }

    // Unmatched candidates spawn tentative tracks.
    for (int c = 0; c < cands.size(); ++c) {
        if (cMatched[c])
            continue;
        LaneTrack t;
        t.id = m_nextId++;
        t.a = cands[c].a; t.b = cands[c].b; t.c = cands[c].c;
        t.nearX = cands[c].nearX;
        t.yTop = cands[c].yTop; t.yBot = cands[c].yBot;
        t.side = t.nearX < 0.5 ? -1 : 1;
        t.hits = 1; t.missed = 0;
        m_tracks.push_back(t);
    }
}

void LaneModel::buildRows()
{
    // Confirmed boundaries this frame, ordered left -> right.
    QVector<const LaneTrack *> confirmed;
    for (const LaneTrack &t : m_tracks)
        if (t.hits >= m_minHits)
            confirmed.push_back(&t);
    std::sort(confirmed.begin(), confirmed.end(),
              [](const LaneTrack *a, const LaneTrack *b) { return a->nearX < b->nearX; });

    // Collapse the per-lane fits into ONE rigid road estimate: a shared
    // curvature (median of the fits) and the ego-lane centre (midpoint of the
    // two boundaries straddling x=0.5; one boundary + the known width also
    // works). Width itself is fixed and set from QML.
    bool haveEstimate = false;
    double estA = 0.0, estB = 0.0, estCenter = m_centerNearX;
    if (!confirmed.isEmpty()) {
        QVector<double> as, bs;
        for (const LaneTrack *t : confirmed) { as.push_back(t->a); bs.push_back(t->b); }
        estA = medianOf(as);
        estB = medianOf(bs);

        const LaneTrack *egoLeft = nullptr, *egoRight = nullptr;
        for (const LaneTrack *t : confirmed) {
            if (t->nearX < kEgoAnchorX) egoLeft = t;   // last left-of-car boundary
            else { egoRight = t; break; }              // first right-of-car boundary
        }
        if (egoLeft && egoRight)
            estCenter = 0.5 * (egoLeft->nearX + egoRight->nearX);
        else if (egoLeft)
            estCenter = egoLeft->nearX + m_laneHalfWidth;
        else if (egoRight)
            estCenter = egoRight->nearX - m_laneHalfWidth;

        // An adjacent lane exists on a side only if a marking lies beyond the
        // ego lane's actual boundary there — never invent a lane.
        const double lb = egoLeft  ? egoLeft->nearX  : (kEgoAnchorX - m_laneHalfWidth);
        const double rb = egoRight ? egoRight->nearX : (kEgoAnchorX + m_laneHalfWidth);
        m_leftAdj = m_rightAdj = false;
        for (const LaneTrack *t : confirmed) {
            if (t->nearX < lb - kAdjMargin) m_leftAdj  = true;
            if (t->nearX > rb + kAdjMargin) m_rightAdj = true;
        }
        haveEstimate = true;
    }

    // Smooth the rigid model, or coast it when this frame gave no estimate.
    if (haveEstimate) {
        if (!m_haveRoad) {
            m_roadA = estA; m_roadB = estB;
            m_centerNearX = estCenter;
            m_haveRoad = true;
        } else {
            m_roadA       = kRoadAlpha * estA      + (1 - kRoadAlpha) * m_roadA;
            m_roadB       = kRoadAlpha * estB      + (1 - kRoadAlpha) * m_roadB;
            m_centerNearX = kRoadAlpha * estCenter + (1 - kRoadAlpha) * m_centerNearX;
        }
        m_roadMissed = 0;
    } else if (m_haveRoad) {
        if (++m_roadMissed > kMaxRoadMissed) {
            m_haveRoad = false;
            m_leftAdj = m_rightAdj = false;
        }
    }

    // Emit the road: blue ego corridor (always) + grey adjacents (when
    // detected). Every boundary is the centre + an integer number of
    // half-widths, so spacing is uniform; only the centre + curvature come
    // from the detections.
    QVector<Row> rows;
    if (m_haveRoad) {
        const double halfW  = m_laneHalfWidth;
        const double center = m_centerNearX;
        const double egoLx  = center - halfW;
        const double egoRx  = center + halfW;

        // Ego corridor fill between the two ego boundaries.
        Row fill; fill.laneType = 2; fill.side = 0;
        QVariantList rightEdge = sampleBoundary(egoRx);
        std::reverse(rightEdge.begin(), rightEdge.end());
        fill.points = sampleBoundary(egoLx);
        fill.points.append(rightEdge);
        rows.push_back(fill);

        // The two ego boundaries (blue).
        Row bl; bl.laneType = 0; bl.side = -1; bl.points = sampleBoundary(egoLx); rows.push_back(bl);
        Row br; br.laneType = 0; br.side = +1; br.points = sampleBoundary(egoRx); rows.push_back(br);

        // Adjacent lane lines (grey), one lane width out, only where seen.
        if (m_leftAdj)  { Row r; r.laneType = 1; r.side = -1; r.points = sampleBoundary(center - 3.0 * halfW); rows.push_back(r); }
        if (m_rightAdj) { Row r; r.laneType = 1; r.side = +1; r.points = sampleBoundary(center + 3.0 * halfW); rows.push_back(r); }

        // Lane-departure signal: how far the car sits from its lane centre.
        m_egoOffset = kEgoAnchorX - center;
        m_egoCurvature = m_roadA;
        m_egoValid = true;
    } else {
        m_egoValid = false;
    }

    const bool countChanged = rows.size() != m_rows.size();
    if (countChanged)
        beginResetModel();
    m_rows = rows;
    if (countChanged)
        endResetModel();
    else if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));

    emit egoChanged();
}

// Sample one road marking: keeps its real bottom position `nearX`, bends by
// the shared road curvature going up, and converges toward the lane centre
// with a perspective taper.
QVariantList LaneModel::sampleBoundary(double nearX) const
{
    QVariantList pts;
    for (int i = 0; i < kSamples; ++i) {
        const double y = kLaneYTop + (kLaneYBot - kLaneYTop) * i / (kSamples - 1);
        const double persp = kPerspTop + (kPerspBot - kPerspTop) * (y - kLaneYTop) / (kLaneYBot - kLaneYTop);
        const double curveDev = m_roadA * (y * y - kLaneYBot * kLaneYBot) + m_roadB * (y - kLaneYBot);
        const double x = m_centerNearX + curveDev + (nearX - m_centerNearX) * persp;
        pts.push_back(QPointF(qBound(-0.3, x, 1.3), y));
    }
    return pts;
}

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
void LaneModel::setLaneHalfWidth(double v)
{
    v = qBound(0.04, v, 0.45);
    if (qFuzzyCompare(m_laneHalfWidth, v)) return;
    m_laneHalfWidth = v;
    buildRows();          // re-emit the corridor at the new width immediately
    emit paramsChanged();
}
void LaneModel::setMaxResidual(double v) { if (qFuzzyCompare(m_maxResidual, v)) return; m_maxResidual = v; emit paramsChanged(); }
void LaneModel::setNearGate(double v)    { if (qFuzzyCompare(m_nearGate, v)) return; m_nearGate = v; emit paramsChanged(); }
void LaneModel::setMinHits(int v)        { if (m_minHits == v) return; m_minHits = v; emit paramsChanged(); }
void LaneModel::setMaxAge(int v)         { if (m_maxAge == v) return; m_maxAge = v; emit paramsChanged(); }
void LaneModel::setEmaAlpha(double v)    { if (qFuzzyCompare(m_emaAlpha, v)) return; m_emaAlpha = v; emit paramsChanged(); }
