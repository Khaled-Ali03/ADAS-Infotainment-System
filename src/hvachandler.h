#ifndef HVACHANDLER_H
#define HVACHANDLER_H

#include <QObject>

// Dual-zone climate state, bound by the BottomBar. Temperatures in Celsius,
// clamped to a sensible cabin range.
class HVACHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double driverTempC READ driverTempC WRITE setDriverTempC NOTIFY driverTempChanged)
    Q_PROPERTY(double passengerTempC READ passengerTempC WRITE setPassengerTempC NOTIFY passengerTempChanged)
    Q_PROPERTY(int fanSpeed READ fanSpeed WRITE setFanSpeed NOTIFY fanSpeedChanged)

public:
    explicit HVACHandler(QObject *parent = nullptr);

    double driverTempC() const { return m_driverTempC; }
    double passengerTempC() const { return m_passengerTempC; }
    int fanSpeed() const { return m_fanSpeed; }

    void setDriverTempC(double t);
    void setPassengerTempC(double t);
    void setFanSpeed(int s);

public slots:
    void driverTempUp()      { setDriverTempC(m_driverTempC + 0.5); }
    void driverTempDown()    { setDriverTempC(m_driverTempC - 0.5); }
    void passengerTempUp()   { setPassengerTempC(m_passengerTempC + 0.5); }
    void passengerTempDown() { setPassengerTempC(m_passengerTempC - 0.5); }

signals:
    void driverTempChanged();
    void passengerTempChanged();
    void fanSpeedChanged();

private:
    static constexpr double kMinTemp = 16.0;
    static constexpr double kMaxTemp = 30.0;

    double m_driverTempC = 21.0;
    double m_passengerTempC = 21.0;
    int m_fanSpeed = 2; // 0..5
};

#endif // HVACHANDLER_H
