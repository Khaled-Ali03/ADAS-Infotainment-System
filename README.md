# GUI
The GUI Team is responsible for designing and developing the user-facing interface of the system. Their primary goal is to ensure that users (e.g., operators, admins, or clients) can interact with the embedded system and cloud services in a clear, intuitive, and responsive manner.

## Project structure

```
MainWindow.qml     # 1024x600 layout engine (home/map states)
UI/                # the four main screen sections
  TopBar.qml  BottomBar.qml  LeftScreen.qml  RightScreen.qml
Components/        # reusable QML pieces used by the screens
  Vehicle3D.qml  DriveCluster.qml  ParkOverlay.qml  NavCard.qml
  MiniMap.qml  MediaPlayer.qml  LockIcon.qml
src/               # C++ backend (models + controllers, registered as QML singletons)
data/              # perception + telemetry JSON captures (copied next to the exe)
resources/         # SVG icons and 3D models (.glb)
docs/              # session changelogs
```

## Build

Qt 6.7.0 (MinGW) + Ninja. Cap parallel jobs at 2 or the QML-cache compiles run out of memory:

```
cmake -S . -B build/cc-build
cmake --build build/cc-build -j 2
```
