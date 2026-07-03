#include "themecontroller.h"

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
}

void ThemeController::setDark(bool d)
{
    if (m_dark == d)
        return;
    m_dark = d;
    emit themeChanged();   // all color props NOTIFY this
}
