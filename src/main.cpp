#include <QQuickStyle>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLoggingCategory>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "detectionmodel.h"
#include "lanemodel.h"
#include "systemhandler.h"
#include "hvachandler.h"
#include "profilemanager.h"
#include "navcontroller.h"
#include "mediacontroller.h"
#include "themecontroller.h"

// The 3D models are loaded from the filesystem (RuntimeLoader can't read qrc):
// look next to the exe first (CMake copies them there), then the source tree.
static QString resolveFile(const QString &name, const QString &srcSubDir)
{
    const QString beside = QCoreApplication::applicationDirPath() + "/" + name;
    if (QFile::exists(beside))
        return beside;
#ifdef ADAS_SOURCE_DIR
    const QString src = QStringLiteral(ADAS_SOURCE_DIR) + "/" + srcSubDir + "/" + name;
    if (QFile::exists(src))
        return src;
#endif
    return beside;
}

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));

    // Live radio streams have no duration, which makes FFmpeg log a bogus
    // "invalid duration" warning per station. Harmless, so silence it.
    QLoggingCategory::setFilterRules(QStringLiteral(
        "qt.multimedia.ffmpeg.mediadataholder.warning=false"));

    QGuiApplication app(argc, argv);

    // C++ backend: detection/lane playback + event-driven controllers.
    auto *detectionModel = new DetectionModel(&app);
    auto *systemHandler = new SystemHandler(detectionModel, &app); // telemetry synced to frames
    auto *hvacHandler   = new HVACHandler(&app);
    auto *profileMgr    = new ProfileManager(&app);
    auto *laneModel      = new LaneModel(detectionModel, &app); // shares frames + clock
    auto *navController   = new NavController(&app);
    auto *mediaController = new MediaController(&app);
    auto *themeController = new ThemeController(&app);
    profileMgr->setHvacHandler(hvacHandler);

    // Expose as QML singletons under the ADAS_HMI module URI.
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "SystemHandler", systemHandler);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "HVACHandler", hvacHandler);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "ProfileManager", profileMgr);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "DetectionModel", detectionModel);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "LaneModel", laneModel);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "NavController", navController);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "MediaController", mediaController);
    qmlRegisterSingletonInstance("ADAS_HMI", 1, 0, "Theme", themeController);

    QQmlApplicationEngine engine;

    // File URLs for the 3D models, used by the Components/ QML.
    engine.rootContext()->setContextProperty(
        QStringLiteral("CarModelUrl"),
        QUrl::fromLocalFile(resolveFile(QStringLiteral("Car.glb"),
                                        QStringLiteral("resources/Vehicles"))));
    engine.rootContext()->setContextProperty(
        QStringLiteral("PedestrianModelUrl"),
        QUrl::fromLocalFile(resolveFile(QStringLiteral("Man.glb"),
                                        QStringLiteral("resources/Pedestrian"))));
    // Traffic models live in one folder; QML appends "SUV.glb" etc. per class.
    const QString suvPath = resolveFile(QStringLiteral("SUV.glb"),
                                        QStringLiteral("resources/Vehicles"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("VehicleModelBase"),
        QUrl::fromLocalFile(QFileInfo(suvPath).absolutePath() + QStringLiteral("/")).toString());

    const QUrl url(QStringLiteral("qrc:/ADAS_HMI/MainWindow.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    QQuickStyle::setStyle("Basic");
    engine.load(url);

    return app.exec();
}
