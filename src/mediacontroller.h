#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QUrl>

class QMediaPlayer;
class QAudioOutput;

// Real audio playback (QMediaPlayer + QAudioOutput) behind the media UI.
// Two sources: Radio (public internet streams) and Local (files scanned from
// `mediaFolder` — USB/SD on the target, Music folder on the dev box). Player
// signals are mirrored onto properties so QML binds without polling.
class MediaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Source source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString albumArt READ albumArt NOTIFY nowPlayingChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool isLive READ isLive NOTIFY nowPlayingChanged)
    Q_PROPERTY(int positionSec READ positionSec NOTIFY positionChanged)
    Q_PROPERTY(int durationSec READ durationSec NOTIFY durationChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY nowPlayingChanged)
    Q_PROPERTY(QVariantList radioStations READ radioStations NOTIFY radioStationsChanged)
    Q_PROPERTY(QVariantList localTracks READ localTracks NOTIFY localTracksChanged)
    Q_PROPERTY(QString mediaFolder READ mediaFolder WRITE setMediaFolder NOTIFY mediaFolderChanged)

public:
    enum Source { Radio = 0, Local = 1 };
    Q_ENUM(Source)

    explicit MediaController(QObject *parent = nullptr);

    Source source() const { return m_source; }
    QString trackTitle() const { return m_trackTitle; }
    QString artist() const { return m_artist; }
    QString albumArt() const { return m_albumArt; }
    bool playing() const;
    bool isLive() const { return m_source == Radio; }
    int positionSec() const { return m_positionMs / 1000; }
    int durationSec() const { return m_durationMs / 1000; }
    QString positionText() const { return formatTime(m_positionMs); }
    QString durationText() const { return formatTime(m_durationMs); }
    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString statusText() const { return m_statusText; }
    int currentIndex() const { return m_index; }
    QVariantList radioStations() const { return m_radioStations; }
    QVariantList localTracks() const { return m_localTracks; }
    QString mediaFolder() const { return m_mediaFolder; }

    void setSource(Source s);
    void setVolume(qreal v);
    void setMuted(bool m);
    void setMediaFolder(const QString &path);

public slots:
    void toggleMute() { setMuted(!m_muted); }
    void togglePlay();
    void play();
    void pause();
    void next();
    void previous();
    void playRadio(int index);
    void playLocal(int index);
    void seekTo(int seconds);
    void rescanLocal();

signals:
    void sourceChanged();
    void nowPlayingChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void statusChanged();
    void radioStationsChanged();
    void localTracksChanged();
    void mediaFolderChanged();

private:
    struct Station { QString name; QString genre; QUrl url; QString art; };
    struct LocalTrack { QString title; QString artist; QUrl url; };

    void loadStations();
    void startCurrent();                 // (re)point the player at m_index and play
    void setStatusText(const QString &s);
    static QString formatTime(qint64 ms);

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;

    Source m_source = Radio;
    int m_index = 0;                     // index into the active source's list

    QVector<Station> m_stations;
    QVector<LocalTrack> m_tracks;
    QVariantList m_radioStations;        // QML-facing mirrors of the above
    QVariantList m_localTracks;

    QString m_trackTitle;
    QString m_artist;
    QString m_albumArt;
    QString m_statusText;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    qreal m_volume = 0.7;
    bool m_muted = false;
    QString m_mediaFolder;
};

#endif // MEDIACONTROLLER_H
