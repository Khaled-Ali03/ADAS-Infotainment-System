#ifndef LANEMODEL_H
#define LANEMODEL_H

#include <QAbstractListModel>
#include <QVector>
#include <QPointF>
#include <QVariantList>

class DetectionModel;

// A road lane stabilized across frames. Geometry is a quadratic
// x = a*y^2 + b*y + c in normalised coords (x,y in [0,1]), which smooths well
// frame to frame and extrapolates to fill gaps.
struct LaneTrack {
    int id = 0;
    double a = 0.0, b = 0.0, c = 0.0; // smoothed quadratic coefficients
    double nearX = 0.0;               // smoothed x at the bottom (y=1), used for matching
    double yTop = 0.0, yBot = 1.0;    // smoothed observed y-range
    int hits = 0;                     // consecutive consistent fits
    int missed = 0;                   // frames coasting without a fit
    int side = 0;                     // -1 left of centre, +1 right
};

// Builds the drivable-lane geometry for the Left Screen from the perception
// JSON's CULane polylines (up to 4/frame, native 1640x590).
//
// The raw stream is noisy (~9% of frames have no lanes, lane index is not a
// stable identity, intersections produce phantom polylines), so this runs a
// small lane tracker: quadratic-fit each raw lane, reject bad fits, match by
// road position, EMA the coefficients, confirm after minHits, coast through
// dropouts. Confirmed lanes are collapsed into one rigid road model and
// exposed as sampled polylines for QtQuick.Shapes.
//
// Shares the loaded frames + clock with DetectionModel; never re-loads the file.
class LaneModel : public QAbstractListModel
{
    Q_OBJECT
    // Ego-lane summary (lane-departure signal).
    Q_PROPERTY(double egoOffset READ egoOffset NOTIFY egoChanged)
    Q_PROPERTY(double egoCurvature READ egoCurvature NOTIFY egoChanged)
    Q_PROPERTY(bool egoValid READ egoValid NOTIFY egoChanged)

    // Fixed ego-lane half-width (normalised x). Set from QML to sit a little
    // wider than the car; only the lane centre + curvature come from the data.
    Q_PROPERTY(double laneHalfWidth READ laneHalfWidth WRITE setLaneHalfWidth NOTIFY paramsChanged)

    // Filter parameters, tunable at runtime.
    Q_PROPERTY(double maxResidual READ maxResidual WRITE setMaxResidual NOTIFY paramsChanged)
    Q_PROPERTY(double nearGate READ nearGate WRITE setNearGate NOTIFY paramsChanged)
    Q_PROPERTY(int minHits READ minHits WRITE setMinHits NOTIFY paramsChanged)
    Q_PROPERTY(int maxAge READ maxAge WRITE setMaxAge NOTIFY paramsChanged)
    Q_PROPERTY(double emaAlpha READ emaAlpha WRITE setEmaAlpha NOTIFY paramsChanged)

public:
    enum Roles {
        PointsRole = Qt::UserRole + 1, // QVariantList<QPointF>, normalised polyline
        LaneTypeRole,                  // 0 = ego boundary, 1 = adjacent, 2 = ego-corridor fill
        SideRole                       // -1 left, +1 right, 0 centre/fill
    };

    explicit LaneModel(DetectionModel *detection, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    double egoOffset() const { return m_egoOffset; }
    double egoCurvature() const { return m_egoCurvature; }
    bool egoValid() const { return m_egoValid; }

    double laneHalfWidth() const { return m_laneHalfWidth; }
    double maxResidual() const { return m_maxResidual; }
    double nearGate() const { return m_nearGate; }
    int minHits() const { return m_minHits; }
    int maxAge() const { return m_maxAge; }
    double emaAlpha() const { return m_emaAlpha; }

    void setLaneHalfWidth(double v);
    void setMaxResidual(double v);
    void setNearGate(double v);
    void setMinHits(int v);
    void setMaxAge(int v);
    void setEmaAlpha(double v);

signals:
    void egoChanged();
    void paramsChanged();

private slots:
    void rebuild(); // recompute lanes for DetectionModel's current frame

private:
    // One renderable lane row for the current frame.
    struct Row {
        QVariantList points; // normalised QPointF polyline
        int laneType = 0;
        int side = 0;
    };

    // A quadratic fit derived from one raw lane this frame.
    struct Candidate { double a, b, c, nearX, yTop, yBot; };

    bool fitQuadratic(const QVector<QPointF> &normPts, Candidate &out) const;
    void updateTracks(const QVector<Candidate> &cands, bool discontinuous);
    void buildRows();
    // Sample one road marking at bottom position `nearX`, bent by the shared
    // road curvature with a perspective taper toward the lane centre.
    QVariantList sampleBoundary(double nearX) const;

    DetectionModel *m_detection = nullptr;
    int m_lastFrame = -1;

    QVector<LaneTrack> m_tracks;
    int m_nextId = 1;
    QVector<Row> m_rows;

    // Rigid road model (smoothed across frames): all lanes share ONE curvature
    // and ONE width, so they can't drift apart or bend independently.
    double m_roadA = 0.0, m_roadB = 0.0;   // shared centreline quadratic
    double m_centerNearX = 0.5;            // ego-lane centre x at the bottom
    double m_laneHalfWidth = 0.16;         // half the ego-lane width, set from QML
    bool m_haveRoad = false;
    int m_roadMissed = 0;                  // frames coasting without a fresh estimate
    bool m_leftAdj = false;                // adjacent lane detected on the left
    bool m_rightAdj = false;               // ... and on the right

    double m_egoOffset = 0.0;
    double m_egoCurvature = 0.0;
    bool m_egoValid = false;

    // Filter parameters.
    double m_maxResidual = 0.035; // max RMS fit error to accept a lane
    double m_nearGate = 0.08;     // max |nearX| delta to match a lane to a track
    int m_minHits = 3;            // fits before a lane becomes visible
    int m_maxAge = 15;            // frames a lane may coast (~0.6 s)
    double m_emaAlpha = 0.3;      // coefficient smoothing factor
};

#endif // LANEMODEL_H
