#ifndef DETECTIONMODEL_H
#define DETECTIONMODEL_H

#include <QAbstractListModel>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QPointF>
#include <QString>

// One raw YOLO detection (center/size derived from bbox_xyxy).
struct Detection {
    QString className;
    double cx = 0.0;        // pixel coords in source resolution
    double cy = 0.0;
    double w  = 0.0;
    double h  = 0.0;
    double confidence = 0.0;
    double depth = 0.0;     // metres from the ego camera, 0 if unknown
};

// One raw lane polyline for a frame (consumed by LaneModel).
struct LaneRaw {
    int index = 0;
    QVector<QPointF> points;
};

// All perception data for a single video frame.
struct FrameData {
    double timestampSec = 0.0;
    QVector<Detection> detections;
    QVector<LaneRaw> lanes;
};

// An object tracked across frames (SORT-style).
struct Track {
    int id = 0;
    QString className;
    double cx = 0.0, cy = 0.0, w = 0.0, h = 0.0; // smoothed pose
    double vx = 0.0, vy = 0.0;                    // smoothed velocity (px/frame)
    double depth = 0.0;                            // smoothed depth (metres)
    double confidence = 0.0;
    int hits = 0;       // total matched detections
    int missed = 0;     // consecutive frames without a match (coasting)
};

// Plays the perception JSON frame-by-frame and runs a lightweight tracker so
// the UI shows stable objects instead of YOLO's flickering per-frame boxes.
// Confirmed tracks are exposed as a list model with granular updates
// (insert/remove/dataChanged) so QML delegates persist and can animate.
class DetectionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int frameIndex READ frameIndex NOTIFY frameIndexChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY loaded)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY loaded)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY loaded)
    Q_PROPERTY(double timestampSec READ timestampSec NOTIFY frameIndexChanged)

    // Tracker parameters, tunable at runtime (take effect next frame).
    Q_PROPERTY(double confThreshold READ confThreshold WRITE setConfThreshold NOTIFY paramsChanged)
    Q_PROPERTY(double iouMatch READ iouMatch WRITE setIouMatch NOTIFY paramsChanged)
    Q_PROPERTY(int maxAge READ maxAge WRITE setMaxAge NOTIFY paramsChanged)
    Q_PROPERTY(int minHits READ minHits WRITE setMinHits NOTIFY paramsChanged)
    Q_PROPERTY(double emaAlpha READ emaAlpha WRITE setEmaAlpha NOTIFY paramsChanged)
    Q_PROPERTY(double gateScale READ gateScale WRITE setGateScale NOTIFY paramsChanged)

public:
    enum Roles {
        ClassNameRole = Qt::UserRole + 1,
        CenterXRole,
        CenterYRole,
        BoxWidthRole,
        BoxHeightRole,
        ConfidenceRole,
        TrackIdRole,
        HeadingRole,      // radians, derived from velocity
        DepthRole         // metres from the ego camera
    };

    explicit DetectionModel(QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int frameIndex() const { return m_frameIndex; }
    int frameCount() const { return m_frames.size(); }
    bool running() const { return m_timer.isActive(); }
    int sourceWidth() const { return m_sourceWidth; }
    int sourceHeight() const { return m_sourceHeight; }
    double timestampSec() const;

    double confThreshold() const { return m_confThreshold; }
    double iouMatch() const { return m_iouMatch; }
    int maxAge() const { return m_maxAge; }
    int minHits() const { return m_minHits; }
    double emaAlpha() const { return m_emaAlpha; }
    double gateScale() const { return m_gateScale; }

    void setConfThreshold(double v);
    void setIouMatch(double v);
    void setMaxAge(int v);
    void setMinHits(int v);
    void setEmaAlpha(double v);
    void setGateScale(double v);

    // Ego forward speed (km/h), pushed by SystemHandler each frame. Used to
    // coast undetected vehicles toward us instead of letting them vanish.
    void setEgoSpeedKmh(double v) { m_egoSpeedKmh = v; }

    // LaneModel shares the loaded frames + clock instead of reloading the file.
    const QVector<FrameData> &frames() const { return m_frames; }

public slots:
    void play();
    void pause();
    void seek(int frame);   // discontinuous: clears tracks

signals:
    void frameIndexChanged();
    void runningChanged();
    void loaded();
    void paramsChanged();
    void scenarioEnded();   // playback reached the last frame

private:
    void advanceFrame();
    void gotoFrame(int frame, bool continuous);
    void processFrame(int frame);
    void syncRows();        // diff confirmed tracks -> granular model signals
    bool isConfirmed(const Track &t) const;
    // Keep a vehicle only if it falls within the three visible lanes.
    bool withinLaneBand(const Track &t) const;
    // Size-vs-depth sanity check: drops giant/tiny phantom boxes.
    bool plausibleDetection(const Detection &d) const;

    bool loadFromFile(const QString &path);
    QString resolveDataPath() const;

    QVector<FrameData> m_frames;
    QTimer m_timer;
    int m_frameIndex = 0;
    int m_sourceWidth = 1280;
    int m_sourceHeight = 720;

    // Tracker state.
    QHash<int, Track> m_tracks;   // id -> track (all alive tracks)
    QVector<int> m_rowIds;        // row order: confirmed/visible track ids
    int m_nextTrackId = 1;

    // Tracker parameters.
    double m_confThreshold = 0.40;
    double m_iouMatch = 0.30;
    int m_maxAge = 8;      // frames a track may coast before removal
    int m_minHits = 3;     // hits before a track becomes visible
    double m_emaAlpha = 0.40;
    double m_nmsIou = 0.45;
    double m_gateScale = 2.0;   // match radius as a multiple of mean box dimension

    // Display filter: the capture is a CARLA scene with three visible lanes,
    // so vehicles outside that band are mis-detections.
    double m_cameraFovDeg = 90.0;  // CARLA RGB camera horizontal FOV
    double m_laneHalfSpan = 5.4;   // metres, half of the 3-lane band

    double m_egoSpeedKmh = 0.0;
};

#endif // DETECTIONMODEL_H
