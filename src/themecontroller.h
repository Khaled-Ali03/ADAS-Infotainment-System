#ifndef THEMECONTROLLER_H
#define THEMECONTROLLER_H

#include <QObject>
#include <QString>

// Central light/dark palette. QML binds semantic colors (window/surface/text…)
// instead of hardcoded hex, so toggle() re-themes everything at once. The OSM
// map tiles stay light in dark mode (needs a dark tile server + cache clear).
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY themeChanged)
    Q_PROPERTY(QString window READ window NOTIFY themeChanged)
    Q_PROPERTY(QString surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QString surfaceAlt READ surfaceAlt NOTIFY themeChanged)
    Q_PROPERTY(QString surfaceMuted READ surfaceMuted NOTIFY themeChanged)
    Q_PROPERTY(QString textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QString textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QString textFaint READ textFaint NOTIFY themeChanged)
    Q_PROPERTY(QString border READ border NOTIFY themeChanged)
    Q_PROPERTY(QString accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QString onAccent READ onAccent NOTIFY themeChanged)
    Q_PROPERTY(QString danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QString gold READ gold NOTIFY themeChanged)

public:
    explicit ThemeController(QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    QString window()        const { return m_dark ? "#000000" : "#F0F0F0"; }
    QString surface()       const { return m_dark ? "#1A1A1A" : "#FFFFFF"; }
    QString surfaceAlt()    const { return m_dark ? "#262626" : "#ECECEC"; }
    QString surfaceMuted()  const { return m_dark ? "#22262C" : "#DCE6F0"; }
    QString textPrimary()   const { return m_dark ? "#FFFFFF" : "#141414"; }
    QString textSecondary() const { return m_dark ? "#999999" : "#666666"; }
    QString textFaint()     const { return m_dark ? "#777777" : "#999999"; }
    QString border()        const { return m_dark ? "#333333" : "#D5D5D5"; }
    QString accent()        const { return "#22AFFB"; }   // brand blue, both modes
    QString onAccent()      const { return "#000000"; }   // text over accent
    QString danger()        const { return m_dark ? "#FF5252" : "#E0312E"; }
    QString gold()          const { return "#C9A227"; }   // Tesla-style gold, both modes

    void setDark(bool d);

public slots:
    void toggle() { setDark(!m_dark); }

signals:
    void themeChanged();

private:
    bool m_dark = false;  // HMI defaults to the light Tesla-style theme
};

#endif // THEMECONTROLLER_H
