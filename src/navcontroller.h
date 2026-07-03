#ifndef NAVCONTROLLER_H
#define NAVCONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QGeoCoordinate>

// State controller behind the RightScreen bottom drawer. Owns the selected
// place, the active panel (Media vs Map bar) and the shared route info; route
// geometry itself stays in QML (RouteModel).
class NavController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Panel panel READ panel WRITE setPanel NOTIFY panelChanged)
    Q_PROPERTY(bool hasPin READ hasPin NOTIFY locationChanged)
    Q_PROPERTY(QString destName READ destName NOTIFY locationChanged)
    Q_PROPERTY(QString destAddress READ destAddress NOTIFY locationChanged)
    Q_PROPERTY(QGeoCoordinate destCoordinate READ destCoordinate NOTIFY locationChanged)
    Q_PROPERTY(bool navigating READ navigating WRITE setNavigating NOTIFY navigatingChanged)

    // Route state, computed once in the RightScreen map, reused by the home
    // nav card and mini-map.
    Q_PROPERTY(bool hasRoute READ hasRoute NOTIFY routeChanged)
    Q_PROPERTY(QVariantList directions READ directions NOTIFY routeChanged)
    Q_PROPERTY(QVariantList routePath READ routePath NOTIFY routeChanged)
    Q_PROPERTY(QString routeDistanceText READ routeDistanceText NOTIFY routeChanged)
    Q_PROPERTY(QString routeDurationText READ routeDurationText NOTIFY routeChanged)
    Q_PROPERTY(QString routeEtaText READ routeEtaText NOTIFY routeChanged)

public:
    enum Panel { Media = 0, Map = 1 };
    Q_ENUM(Panel)

    explicit NavController(QObject *parent = nullptr);

    Panel panel() const { return m_panel; }
    bool hasPin() const { return m_hasPin; }
    QString destName() const { return m_destName; }
    QString destAddress() const { return m_destAddress; }
    QGeoCoordinate destCoordinate() const { return m_destCoordinate; }
    bool navigating() const { return m_navigating; }

    bool hasRoute() const { return !m_directions.isEmpty() || !m_routePath.isEmpty(); }
    QVariantList directions() const { return m_directions; }
    QVariantList routePath() const { return m_routePath; }
    QString routeDistanceText() const { return m_routeDistanceText; }
    QString routeDurationText() const { return m_routeDurationText; }
    QString routeEtaText() const { return m_routeEtaText; }

    void setPanel(Panel p);
    void setNavigating(bool n);

public slots:
    // Map tap / search-result select: store the place and show the Map bar.
    void dropPin(const QGeoCoordinate &coord);
    // Fill in name/address once reverse-geocoding resolves.
    void updateLocationInfo(const QString &name, const QString &address);
    void clearPin();
    void showMedia() { setPanel(Media); }
    void showMap()   { setPanel(Map); }

    // Called by the RightScreen map when a route resolves / is cancelled.
    void setRoute(const QVariantList &steps, const QVariantList &path,
                  const QString &distance, const QString &duration, const QString &eta);
    void clearRoute();

signals:
    void panelChanged();
    void locationChanged();
    void navigatingChanged();
    void routeChanged();

private:
    Panel m_panel = Media;
    bool m_hasPin = false;
    QString m_destName;
    QString m_destAddress;
    QGeoCoordinate m_destCoordinate;
    bool m_navigating = false;

    QVariantList m_directions;
    QVariantList m_routePath;
    QString m_routeDistanceText;
    QString m_routeDurationText;
    QString m_routeEtaText;
};

#endif // NAVCONTROLLER_H
