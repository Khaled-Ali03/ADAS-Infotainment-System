#include "hvachandler.h"

#include <QtGlobal>

HVACHandler::HVACHandler(QObject *parent)
    : QObject(parent)
{
}

void HVACHandler::setDriverTempC(double t)
{
    t = qBound(kMinTemp, t, kMaxTemp);
    if (qFuzzyCompare(m_driverTempC, t))
        return;
    m_driverTempC = t;
    emit driverTempChanged();
}

void HVACHandler::setPassengerTempC(double t)
{
    t = qBound(kMinTemp, t, kMaxTemp);
    if (qFuzzyCompare(m_passengerTempC, t))
        return;
    m_passengerTempC = t;
    emit passengerTempChanged();
}

void HVACHandler::setFanSpeed(int s)
{
    s = qBound(0, s, 5);
    if (m_fanSpeed == s)
        return;
    m_fanSpeed = s;
    emit fanSpeedChanged();
}
