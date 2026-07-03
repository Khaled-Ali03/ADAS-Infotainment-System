#include "profilemanager.h"
#include "hvachandler.h"

#include <QStandardPaths>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ProfileManager::ProfileManager(QObject *parent)
    : QObject(parent)
{
    m_dbReady = initDatabase();
    if (m_dbReady) {
        seedDefaults();
        refreshProfileNames();
    }
}

bool ProfileManager::initDatabase()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("adas_profiles.db"));

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "ProfileManager: failed to open DB" << db.lastError().text();
        return false;
    }

    QSqlQuery q;
    const bool ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS profiles ("
        "  name TEXT PRIMARY KEY,"
        "  driver_temp REAL,"
        "  passenger_temp REAL,"
        "  theme TEXT,"
        "  seat_position INTEGER)"));
    if (!ok)
        qWarning() << "ProfileManager: create table failed" << q.lastError().text();
    return ok;
}

void ProfileManager::seedDefaults()
{
    QSqlQuery count;
    count.exec(QStringLiteral("SELECT COUNT(*) FROM profiles"));
    if (count.next() && count.value(0).toInt() > 0)
        return; // already seeded

    struct Seed { const char *name; double drv; double pas; const char *theme; int seat; };
    const Seed seeds[] = {
        { "User A", 21.5, 22.0, "dark",  3 },
        { "User B", 20.0, 20.5, "light", 5 },
    };

    QSqlQuery ins;
    ins.prepare(QStringLiteral(
        "INSERT INTO profiles (name, driver_temp, passenger_temp, theme, seat_position) "
        "VALUES (?, ?, ?, ?, ?)"));
    for (const Seed &s : seeds) {
        ins.addBindValue(QString::fromLatin1(s.name));
        ins.addBindValue(s.drv);
        ins.addBindValue(s.pas);
        ins.addBindValue(QString::fromLatin1(s.theme));
        ins.addBindValue(s.seat);
        if (!ins.exec())
            qWarning() << "ProfileManager: seed failed" << ins.lastError().text();
    }
}

void ProfileManager::refreshProfileNames()
{
    m_profileNames.clear();
    QSqlQuery q(QStringLiteral("SELECT name FROM profiles ORDER BY name"));
    while (q.next())
        m_profileNames << q.value(0).toString();
    emit profilesChanged();
}

QString ProfileManager::loadProfile(const QString &name)
{
    if (!m_dbReady)
        return QString();

    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT driver_temp, passenger_temp, theme FROM profiles WHERE name = ?"));
    q.addBindValue(name);
    if (!q.exec() || !q.next()) {
        qWarning() << "ProfileManager: profile not found" << name;
        return QString();
    }

    if (m_hvac) {
        m_hvac->setDriverTempC(q.value(0).toDouble());
        m_hvac->setPassengerTempC(q.value(1).toDouble());
    }

    m_currentProfile = name;
    emit currentProfileChanged();
    return q.value(2).toString(); // theme
}
