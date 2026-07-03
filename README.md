# Intelligent Vehicle HMI (Qt/QML)

![Qt 6](https://img.shields.io/badge/Qt-6.7.0-41CD52?logo=qt&logoColor=white)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

A high-performance, hardware-accelerated Automotive Human-Machine Interface (HMI) built purely with **Qt 6 / QML** and **C++**. This project serves as the central infotainment and dashboard cluster for an intelligent vehicle prototype, designed to run on a BeagleBone AI-64 embedded platform.

## Key Features

* **Tesla-Inspired Dual Layout:** A fluid, state-driven declarative QML architecture that transitions seamlessly between a 3D Park/Home state and an active Drive/Navigation state without relying on legacy C++ widgets.
* **Real-Time ADAS Visualization:** Translates raw AI inference data (JSON via MQTT) into clean, coordinate-based QML icons. Features a custom **Ego-Motion Tracker** to eliminate detection flicker and stabilize object rendering.
* **Interactive Navigation:** Integrated OpenStreetMap (OSM) plugin with a custom-built API debouncer to prevent server rate-limiting. Features turn-by-turn path rendering, dynamic distance sorting, and reverse geocoding (Drop-a-Pin).
* **Live Environmental Telemetry:** Dual-zone HVAC control panel integrated with live, localized weather data via the Open-Meteo API using native network polling and fallback heartbeats.
* **Hardware-Accelerated UI:** Strict separation of concerns. Heavy data parsing and networking are isolated in the C++ backend, allowing the QML Scene Graph to render 60fps animations entirely on the GPU.

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
```

## Build Instructions

Qt 6.7.0 (MinGW) + Ninja. Cap parallel jobs at 2 or the QML-cache compiles run out of memory:

```
cmake -S . -B build/cc-build
cmake --build build/cc-build -j 2
```

License & Acknowledgments
This project is licensed under the MIT License.

Disclaimer: The UI layout and UX design are inspired by Tesla's Model 3/Y infotainment system. This project is strictly for educational and portfolio purposes and is not affiliated with, endorsed by, or connected to Tesla, Inc.

3D Assets: The excellent low-poly vehicle 3D models were created by [Quaternius](https://quaternius.com/packs/cars.html) (available via CC0 Public Domain).

UI Icons: Vector iconography was sourced from various creators via [SVG Repo](https://www.svgrepo.com/).

Weather Data: Live environmental telemetry is powered by the [Open-Meteo API](https://open-meteo.com/).
