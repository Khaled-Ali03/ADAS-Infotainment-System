#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H

#include <QObject>
#include <QStringList>
#include <QString>

class HVACHandler;

// Profile store backed by SQLite. Opens a local .db, seeds two default driver
// profiles, and applies a selected profile's preferences to the live handlers.
class ProfileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList profileNames READ profileNames NOTIFY profilesChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile NOTIFY currentProfileChanged)

public:
    explicit ProfileManager(QObject *parent = nullptr);

    // Optional wiring so loadProfile() can push HVAC prefs into the live handler.
    void setHvacHandler(HVACHandler *hvac) { m_hvac = hvac; }

    QStringList profileNames() const { return m_profileNames; }
    QString currentProfile() const { return m_currentProfile; }

public slots:
    // Loads a profile by name; applies its HVAC temps and returns its theme.
    QString loadProfile(const QString &name);

signals:
    void profilesChanged();
    void currentProfileChanged();

private:
    bool initDatabase();
    void seedDefaults();
    void refreshProfileNames();

    HVACHandler *m_hvac = nullptr;
    QStringList m_profileNames;
    QString m_currentProfile;
    bool m_dbReady = false;
};

#endif // PROFILEMANAGER_H
