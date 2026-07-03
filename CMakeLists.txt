cmake_minimum_required(VERSION 3.16)

project(ADAS_HMI VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Svg Quick QuickControls2 Location Positioning VirtualKeyboard Sql Multimedia Quick3D)

qt_standard_project_setup()

qt_add_executable(appADAS_HMI
    src/main.cpp
    src/detectionmodel.h src/detectionmodel.cpp
    src/lanemodel.h src/lanemodel.cpp
    src/systemhandler.h src/systemhandler.cpp
    src/hvachandler.h src/hvachandler.cpp
    src/profilemanager.h src/profilemanager.cpp
    src/navcontroller.h src/navcontroller.cpp
    src/mediacontroller.h src/mediacontroller.cpp
    src/themecontroller.h src/themecontroller.cpp
)

# Icons, registered under qrc:/icons/... (listing a .qrc as a source compiles it
# but doesn't register the prefix at runtime, so use qt_add_resources instead).
qt_add_resources(appADAS_HMI "icons"
    PREFIX "/icons"
    FILES
        resources/car.svg
        resources/pin.svg
        resources/person.svg
        resources/car_top.svg
        resources/truck.svg
        resources/bus.svg
        resources/motorcycle.svg
        resources/bicycle.svg
        resources/traffic_light.svg
        resources/stop_sign.svg
        resources/album_art.svg
        resources/radio.svg
        resources/home.svg
        resources/steering_wheel.svg
        resources/Closed_Lock.svg
        resources/Open_Lock.svg
)

# Lets the models fall back to the source tree data/ during development.
target_compile_definitions(appADAS_HMI PRIVATE
    ADAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Big JSON captures are loaded from disk next to the exe (embedding them in qrc
# bloats the build), and the .glb models must be on the filesystem anyway
# because RuntimeLoader's glTF importer can't read from qrc.
set(_runtime_files
    "data/01.Video_60sec_integrated.json"
    "data/ego_telemetry.json"
    "resources/Vehicles/Car.glb"
    "resources/Vehicles/SUV.glb"
    "resources/Vehicles/Motorcycle.glb"
    "resources/Vehicles/Bicycle.glb"
    "resources/Pedestrian/Man.glb"
)
foreach(_f ${_runtime_files})
    get_filename_component(_name "${_f}" NAME)
    add_custom_command(TARGET appADAS_HMI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
            "$<TARGET_FILE_DIR:appADAS_HMI>/${_name}"
    )
endforeach()

qt_add_qml_module(appADAS_HMI
    URI ADAS_HMI
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        MainWindow.qml
        UI/TopBar.qml
        UI/BottomBar.qml
        UI/LeftScreen.qml
        UI/RightScreen.qml
        Components/Vehicle3D.qml
        Components/DriveCluster.qml
        Components/ParkOverlay.qml
        Components/NavCard.qml
        Components/MiniMap.qml
        Components/MediaPlayer.qml
        Components/LockIcon.qml
)

set_target_properties(appADAS_HMI PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appADAS_HMI
    PRIVATE
        Qt6::Svg
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Location
        Qt6::Positioning
        Qt6::VirtualKeyboard
        Qt6::Sql
        Qt6::Multimedia
        Qt6::Quick3D
)

include(GNUInstallDirs)
install(TARGETS appADAS_HMI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
