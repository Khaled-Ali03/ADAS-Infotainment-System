#include "navcontroller.h"

NavController::NavController(QObject *parent)
    : QObject(parent)
{
}

void NavController::setPanel(Panel p)
{
    if (m_panel == p)
        return;
    m_panel = p;
    emit panelChanged();
}

void NavController::setNavigating(bool n)
{
    if (m_navigating == n)
        return;
    m_navigating = n;
    emit navigatingChanged();
}

void NavController::dropPin(const QGeoCoordinate &coord)
{
    m_destCoordinate = coord;
    m_destName = QStringLiteral("Dropped Pin");
    m_destAddress.clear();
    m_hasPin = true;
    emit locationChanged();
    setPanel(Map);   // slide the location details into view
}

void NavController::updateLocationInfo(const QString &name, const QString &address)
{
    bool changed = false;
    if (!name.isEmpty() && name != m_destName) {
        m_destName = name;
        changed = true;
    }
    if (address != m_destAddress) {
        m_destAddress = address;
        changed = true;
    }
    if (changed)
        emit locationChanged();
}

void NavController::clearPin()
{
    if (!m_hasPin && m_destName.isEmpty())
        return;
    m_hasPin = false;
    m_destName.clear();
    m_destAddress.clear();
    m_destCoordinate = QGeoCoordinate();
    setNavigating(false);
    emit locationChanged();
}

void NavController::setRoute(const QVariantList &steps, const QVariantList &path,
                            const QString &distance, const QString &duration,
                            const QString &eta)
{
    m_directions = steps;
    m_routePath = path;
    m_routeDistanceText = distance;
    m_routeDurationText = duration;
    m_routeEtaText = eta;
    emit routeChanged();
}

void NavController::clearRoute()
{
    if (m_directions.isEmpty() && m_routePath.isEmpty())
        return;
    m_directions.clear();
    m_routePath.clear();
    m_routeDistanceText.clear();
    m_routeDurationText.clear();
    m_routeEtaText.clear();
    emit routeChanged();
}
