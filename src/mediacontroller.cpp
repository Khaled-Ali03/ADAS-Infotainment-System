#include "mediacontroller.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QMediaMetaData>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>

namespace {
constexpr const char *kRadioArt = "qrc:/icons/resources/radio.svg";
constexpr const char *kLocalArt = "qrc:/icons/resources/album_art.svg";
}

MediaController::MediaController(QObject *parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audio  = new QAudioOutput(this);
    m_audio->setVolume(static_cast<float>(m_volume));
    m_player->setAudioOutput(m_audio);

    // Mirror every relevant player signal onto our properties (no polling).
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        m_positionMs = p;
        emit positionChanged();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        // Live streams report a bogus/negative duration (INT64_MIN); the UI shows
        // a LIVE badge for radio, but clamp so nothing downstream sees garbage.
        m_durationMs = d > 0 ? d : 0;
        emit durationChanged();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState) { emit playingChanged(); });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this]() {
        // ICY stream metadata: radio stations push the current song as Title.
        if (m_source == Radio) {
            const QString live = m_player->metaData().stringValue(QMediaMetaData::Title);
            if (!live.isEmpty() && live != m_trackTitle) {
                m_trackTitle = live;
                emit nowPlayingChanged();
            }
        }
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
        switch (status) {
        case QMediaPlayer::LoadingMedia:   setStatusText(QStringLiteral("Connecting…")); break;
        case QMediaPlayer::BufferingMedia: setStatusText(QStringLiteral("Buffering…"));  break;
        case QMediaPlayer::StalledMedia:   setStatusText(QStringLiteral("Stalled…"));    break;
        case QMediaPlayer::BufferedMedia:
        case QMediaPlayer::LoadedMedia:    setStatusText(QString());                     break;
        case QMediaPlayer::EndOfMedia:     next(); break;   // auto-advance local tracks
        default: break;
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &msg) {
        setStatusText(msg.isEmpty() ? QStringLiteral("Playback error") : msg);
    });

    // Stop the stream cleanly before the network stack tears down, which avoids
    // (most of) FFmpeg's "tls ... Failed to send close message" noise on exit.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_player) m_player->stop();
    });

    loadStations();

    // Default media folder = the dev box Music location; on the target this is
    // re-pointed at the USB / SD mount via setMediaFolder() from QML/config.
    m_mediaFolder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    rescanLocal();

    // Seed now-playing with the first station but stay paused on boot.
    if (!m_stations.isEmpty()) {
        m_trackTitle = m_stations.first().name;
        m_artist     = m_stations.first().genre;
        m_albumArt   = m_stations.first().art;
        m_player->setSource(m_stations.first().url);
    }
    emit nowPlayingChanged();
}

void MediaController::loadStations()
{
    // Public always-on internet radio / live broadcasts (SomaFM, 128k MP3).
    m_stations = {
        { QStringLiteral("Groove Salad"),     QStringLiteral("Ambient / Downtempo"),
          QUrl(QStringLiteral("https://ice1.somafm.com/groovesalad-128-mp3")), QString::fromLatin1(kRadioArt) },
        { QStringLiteral("Drone Zone"),       QStringLiteral("Atmospheric Ambient"),
          QUrl(QStringLiteral("https://ice1.somafm.com/dronezone-128-mp3")),  QString::fromLatin1(kRadioArt) },
        { QStringLiteral("Lush"),             QStringLiteral("Vocal Electronica"),
          QUrl(QStringLiteral("https://ice1.somafm.com/lush-128-mp3")),       QString::fromLatin1(kRadioArt) },
        { QStringLiteral("Indie Pop Rocks!"), QStringLiteral("Indie"),
          QUrl(QStringLiteral("https://ice1.somafm.com/indiepop-128-mp3")),   QString::fromLatin1(kRadioArt) },
        { QStringLiteral("SF 10-33"),         QStringLiteral("Live SF Dispatch"),
          QUrl(QStringLiteral("https://ice1.somafm.com/sf1033-128-mp3")),     QString::fromLatin1(kRadioArt) },
    };

    m_radioStations.clear();
    for (const Station &s : m_stations) {
        m_radioStations.append(QVariantMap{
            { QStringLiteral("name"), s.name },
            { QStringLiteral("subtitle"), s.genre },
            { QStringLiteral("art"), s.art },
        });
    }
    emit radioStationsChanged();
}

void MediaController::rescanLocal()
{
    m_tracks.clear();
    m_localTracks.clear();

    QDir dir(m_mediaFolder);
    if (dir.exists()) {
        const QStringList filters{ QStringLiteral("*.mp3"), QStringLiteral("*.flac"),
                                   QStringLiteral("*.wav"), QStringLiteral("*.ogg"),
                                   QStringLiteral("*.m4a") };
        const QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            m_tracks.append({ fi.completeBaseName(), QStringLiteral("Local file"),
                              QUrl::fromLocalFile(fi.absoluteFilePath()) });
        }
    }

    for (const LocalTrack &t : m_tracks) {
        m_localTracks.append(QVariantMap{
            { QStringLiteral("name"), t.title },
            { QStringLiteral("subtitle"), t.artist },
            { QStringLiteral("art"), QString::fromLatin1(kLocalArt) },
        });
    }
    emit localTracksChanged();
}

bool MediaController::playing() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

void MediaController::setSource(Source s)
{
    if (m_source == s)
        return;
    m_source = s;
    m_index = 0;
    emit sourceChanged();
}

void MediaController::setVolume(qreal v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_volume, v))
        return;
    m_volume = v;
    if (m_audio)
        m_audio->setVolume(static_cast<float>(v));
    emit volumeChanged();
}

void MediaController::setMuted(bool m)
{
    if (m_muted == m)
        return;
    m_muted = m;
    if (m_audio)
        m_audio->setMuted(m);
    emit mutedChanged();
}

void MediaController::setMediaFolder(const QString &path)
{
    if (m_mediaFolder == path)
        return;
    m_mediaFolder = path;
    emit mediaFolderChanged();
    rescanLocal();
}

void MediaController::startCurrent()
{
    if (m_source == Radio) {
        if (m_index < 0 || m_index >= m_stations.size())
            return;
        const Station &s = m_stations[m_index];
        m_trackTitle = s.name;
        m_artist     = s.genre;
        m_albumArt   = s.art;
        m_player->setSource(s.url);
    } else {
        if (m_index < 0 || m_index >= m_tracks.size())
            return;
        const LocalTrack &t = m_tracks[m_index];
        m_trackTitle = t.title;
        m_artist     = t.artist;
        m_albumArt   = QString::fromLatin1(kLocalArt);
        m_player->setSource(t.url);
    }
    emit nowPlayingChanged();
    m_player->play();
}

void MediaController::togglePlay()
{
    if (playing())
        pause();
    else
        play();
}

void MediaController::play()
{
    // If nothing is queued yet (cold start), point at the current selection.
    if (m_player->source().isEmpty())
        startCurrent();
    else
        m_player->play();
}

void MediaController::pause()
{
    m_player->pause();
}

void MediaController::next()
{
    const int count = (m_source == Radio) ? m_stations.size() : m_tracks.size();
    if (count == 0)
        return;
    m_index = (m_index + 1) % count;
    startCurrent();
}

void MediaController::previous()
{
    const int count = (m_source == Radio) ? m_stations.size() : m_tracks.size();
    if (count == 0)
        return;
    m_index = (m_index - 1 + count) % count;
    startCurrent();
}

void MediaController::playRadio(int index)
{
    if (index < 0 || index >= m_stations.size())
        return;
    if (m_source != Radio) {
        m_source = Radio;
        emit sourceChanged();
    }
    m_index = index;
    startCurrent();
}

void MediaController::playLocal(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    if (m_source != Local) {
        m_source = Local;
        emit sourceChanged();
    }
    m_index = index;
    startCurrent();
}

void MediaController::seekTo(int seconds)
{
    if (m_source == Local && m_durationMs > 0)
        m_player->setPosition(static_cast<qint64>(seconds) * 1000);
}

void MediaController::setStatusText(const QString &s)
{
    if (m_statusText == s)
        return;
    m_statusText = s;
    emit statusChanged();
}

QString MediaController::formatTime(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("0:00");
    const qint64 totalSec = ms / 1000;
    const qint64 m = totalSec / 60;
    const qint64 s = totalSec % 60;
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
