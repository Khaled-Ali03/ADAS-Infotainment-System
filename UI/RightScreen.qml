import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning
import ADAS_HMI

// Map view: OpenStreetMap navigation plus the bottom drawer, which swaps
// horizontally between a media player and a "map bar" (idle card / dropped-pin
// details), and expands vertically to reveal the station/track browser.
// Active panel + selected place are owned by NavController.
Item {
    id: rightScreen
    clip: true

    property real defaultLat: 29.8402
    property real defaultLon: 31.3005
    property var currentPos: QtPositioning.coordinate(defaultLat, defaultLon)

    readonly property int drawerHeight: 120     // retracted
    readonly property int expandedHeight: 410   // extracted (media browser open)
    property bool drawerExpanded: false

    // True while the pointer is over the drawer; used to disable the map's own
    // gestures so they don't fire underneath it.
    property bool bottomHovered: drawerHover.hovered

    // ---- API rate limiter: Nominatim allows ~1 request/second ----
    property real lastApiCallTime: 0

    Timer {
        id: globalApiRateLimiter
        interval: 1000
        repeat: false
        property string pendingQuery: ""
        property var pendingCoord: null
        property bool isReverse: false

        onTriggered: {
            lastApiCallTime = Date.now();
            if (isReverse && pendingCoord) {
                reverseGeocodeModel.query = pendingCoord;
                reverseGeocodeModel.update();
            } else if (!isReverse && pendingQuery.length > 2) {
                geocodeModel.query = pendingQuery;
                geocodeModel.update();
            }
        }
    }

    function safeApiCall(queryData, isReverseReq) {
        let now = Date.now();
        let timeSinceLast = now - lastApiCallTime;

        if (timeSinceLast >= 1000 && !globalApiRateLimiter.running) {
            lastApiCallTime = now;
            if (isReverseReq) {
                reverseGeocodeModel.query = queryData;
                reverseGeocodeModel.update();
            } else {
                geocodeModel.query = queryData;
                geocodeModel.update();
            }
        } else {
            globalApiRateLimiter.isReverse = isReverseReq;
            if (isReverseReq) globalApiRateLimiter.pendingCoord = queryData;
            else globalApiRateLimiter.pendingQuery = queryData;

            if (!globalApiRateLimiter.running) {
                globalApiRateLimiter.interval = Math.max(100, 1000 - timeSinceLast);
                globalApiRateLimiter.start();
            }
        }
    }

    // ---- formatting helpers ----
    function formatDistanceStr(distMeters) {
        return (distMeters / 1000).toFixed(1) + " km";
    }
    function formatManeuverDist(distMeters) {
        if (distMeters >= 1000) return (distMeters / 1000).toFixed(1) + " km";
        return Math.round(distMeters) + " m";
    }
    function formatDuration(sec) {
        let mins = Math.round(sec / 60);
        if (mins < 60) return mins + " min";
        let h = Math.floor(mins / 60);
        return h + " h " + (mins % 60) + " min";
    }
    function directionGlyph(dir) {
        switch (dir) {
        case RouteManeuver.DirectionForward:    return "↑";
        case RouteManeuver.DirectionBearRight:
        case RouteManeuver.DirectionLightRight: return "↗";
        case RouteManeuver.DirectionRight:      return "→";
        case RouteManeuver.DirectionHardRight:  return "↘";
        case RouteManeuver.DirectionUTurnRight:
        case RouteManeuver.DirectionUTurnLeft:  return "↩";
        case RouteManeuver.DirectionHardLeft:   return "↙";
        case RouteManeuver.DirectionLeft:       return "←";
        case RouteManeuver.DirectionBearLeft:
        case RouteManeuver.DirectionLightLeft:  return "↖";
        default:                                return "•";
        }
    }

    // Build the browse list with the currently-playing item pinned on top.
    function buildBrowseItems() {
        var src = (MediaController.source === 0) ? MediaController.radioStations
                                                 : MediaController.localTracks;
        var cur = MediaController.currentIndex;
        var arr = [];
        if (cur >= 0 && cur < src.length) {
            var c = src[cur];
            arr.push({ "name": c.name, "subtitle": c.subtitle, "art": c.art, "origIndex": cur, "current": true });
        }
        for (var i = 0; i < src.length; i++) {
            if (i === cur) continue;
            var it = src[i];
            arr.push({ "name": it.name, "subtitle": it.subtitle, "art": it.art, "origIndex": i, "current": false });
        }
        return arr;
    }
    property var browseItems: (MediaController.source, MediaController.currentIndex,
                               MediaController.radioStations, MediaController.localTracks,
                               buildBrowseItems())

    // Retract the drawer when the Map bar takes over, and keep the map in sync
    // when a trip is cancelled elsewhere (e.g. the home card's Cancel Trip).
    Connections {
        target: NavController
        function onPanelChanged() {
            if (NavController.panel === 1) rightScreen.drawerExpanded = false;
        }
        function onRouteChanged() {
            if (!NavController.hasRoute) {
                routeQuery.clearWaypoints();
                routeModel.reset();
            }
        }
        function onLocationChanged() {
            if (!NavController.hasPin) droppedPin.visible = false;
        }
    }

    // ---- Geocoding / routing engines ----
    Plugin {
        id: mapPlugin
        name: "osm"
        // Dark map tiles would need a different tile host + clearing the Qt
        // tile cache, so the map stays light even in dark mode.
        PluginParameter { name: "osm.mapping.cache.disk.size"; value: 0 }
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: "true" }
        PluginParameter { name: "osm.mapping.host"; value: "https://tile.openstreetmap.org/" }
        PluginParameter { name: "osm.geocoding.host"; value: "https://nominatim.openstreetmap.org" }
    }

    PositionSource {
        id: positionSource
        updateInterval: 1000
        active: true
        onPositionChanged: {
            if (position.coordinate.isValid) {
                currentPos = position.coordinate;
                carMarker.coordinate = currentPos;
            }
        }
    }

    ListModel { id: sortedResultsModel }

    GeocodeModel {
        id: geocodeModel
        plugin: mapPlugin
        bounds: map.visibleRegion.boundingBox
        onStatusChanged: {
            if (status === GeocodeModel.Ready) {
                sortedResultsModel.clear();
                let results = [];
                for (let i = 0; i < count; i++) {
                    let loc = get(i);
                    let dist = currentPos.isValid ? currentPos.distanceTo(loc.coordinate) : 999999;
                    results.push({
                        "titleText": loc.address.street || loc.address.city || loc.address.text,
                        "subtitleText": loc.address.text,
                        "coordLat": loc.coordinate.latitude,
                        "coordLon": loc.coordinate.longitude,
                        "distMeters": dist
                    });
                }
                results.sort((a, b) => a.distMeters - b.distMeters);
                for (let j = 0; j < results.length; j++) {
                    sortedResultsModel.append(results[j]);
                }
            }
        }
    }

    GeocodeModel {
        id: reverseGeocodeModel
        plugin: mapPlugin
        onStatusChanged: {
            if (status === GeocodeModel.Ready && count > 0) {
                let loc = get(0);
                NavController.updateLocationInfo(
                    loc.address.street || loc.address.text || "Dropped Pin",
                    loc.address.text);
            }
        }
    }

    // ---- The map ----
    Map {
        id: map
        anchors.fill: parent
        plugin: mapPlugin
        center: currentPos
        zoomLevel: 14

        // Gestures are disabled over the bottom drawer so they don't leak through.
        DragHandler {
            id: drag; target: null; enabled: !rightScreen.bottomHovered
            onTranslationChanged: (delta) => {
                searchInput.focus = false;
                map.pan(-delta.x, -delta.y);
            }
        }

        WheelHandler {
            id: wheel; enabled: !rightScreen.bottomHovered
            onWheel: (event) => {
                searchInput.focus = false;
                map.zoomLevel += (event.angleDelta.y > 0 ? 0.5 : -0.5);
            }
        }

        // Two-finger pinch zoom (touchscreen / laptop trackpad).
        PinchHandler {
            id: pinch
            target: null
            enabled: !rightScreen.bottomHovered
            property real startZoom: 0
            onActiveChanged: if (active) startZoom = map.zoomLevel
            onActiveScaleChanged: if (active) map.zoomLevel = startZoom + Math.log2(activeScale)
        }

        TapHandler {
            enabled: !rightScreen.bottomHovered
            onTapped: (eventPoint) => {
                searchInput.focus = false;
                // A tap on the map retracts the open drawer instead of dropping
                // a pin (lets the user "click the map to close" the list).
                if (rightScreen.drawerExpanded) { rightScreen.drawerExpanded = false; return; }

                var clickedCoord = map.toCoordinate(eventPoint.position);
                droppedPin.coordinate = clickedCoord;
                droppedPin.visible = true;

                // NavController swaps the drawer to the Map bar; the reverse
                // geocode fills in the address shortly after.
                NavController.dropPin(clickedCoord);
                safeApiCall(clickedCoord, true);
            }
        }

        MapQuickItem {
            id: carMarker
            coordinate: currentPos
            anchorPoint.x: carIcon.width / 2
            anchorPoint.y: carIcon.height / 2
            // Heading-rotated arrow (telemetry yaw) instead of a plain dot.
            sourceItem: Item {
                id: carIcon
                width: 34; height: 34
                Rectangle {
                    anchors.fill: parent; radius: width / 2
                    color: Theme.accent; border.color: "white"; border.width: 3
                }
                Canvas {
                    id: headingArrow
                    anchors.fill: parent
                    rotation: SystemHandler.heading
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "white";
                        ctx.beginPath();
                        ctx.moveTo(width / 2, height * 0.22);
                        ctx.lineTo(width * 0.72, height * 0.66);
                        ctx.lineTo(width / 2, height * 0.56);
                        ctx.lineTo(width * 0.28, height * 0.66);
                        ctx.closePath();
                        ctx.fill();
                    }
                }
            }
        }

        MapQuickItem {
            id: droppedPin
            visible: false
            anchorPoint.x: 20
            anchorPoint.y: 40
            sourceItem: Image {
                source: "qrc:/icons/resources/pin.svg"
                width: 40
                height: 40
                sourceSize.width: 40
                sourceSize.height: 40
            }
        }

        RouteModel {
            id: routeModel
            plugin: mapPlugin
            query: RouteQuery { id: routeQuery }
            onStatusChanged: {
                if (status === RouteModel.Ready && count > 0) {
                    var route = get(0);
                    map.visibleRegion = route.bounds;

                    var steps = [];
                    var segs = route.segments;
                    for (var i = 0; i < segs.length; i++) {
                        var mvr = segs[i].maneuver;
                        if (mvr && mvr.valid) {
                            steps.push({
                                "instruction": mvr.instructionText,
                                "distanceText": formatManeuverDist(mvr.distanceToNextInstruction),
                                "dirGlyph": directionGlyph(mvr.direction)
                            });
                        }
                    }

                    var dist = formatDistanceStr(route.distance);
                    var dur = formatDuration(route.travelTime);
                    var eta = Qt.formatTime(new Date(Date.now() + route.travelTime * 1000), "h:mm AP");

                    // Share route with the home card + mini-map via NavController.
                    NavController.setRoute(steps, route.path, dist, dur, eta);
                }
            }
        }

        MapItemView {
            model: routeModel
            delegate: MapRoute { route: routeData; line.color: Theme.accent; line.width: 6; smooth: true; opacity: 0.85 }
        }
    }

    // ---- Map chrome: theme toggle + zoom + recenter, clear of the drawer ----
    Column {
        anchors.right: parent.right; anchors.margins: 20
        anchors.bottom: parent.bottom; anchors.bottomMargin: drawerHeight + 20
        spacing: 12

        component MapChromeButton : Rectangle {
            property string glyph: ""
            signal activated()
            width: 48; height: 48; radius: 24
            color: Theme.surface; border.color: Theme.border
            Text { anchors.centerIn: parent; text: glyph; color: Theme.textPrimary; font.pixelSize: 22 }
            MouseArea { anchors.fill: parent; onClicked: parent.activated() }
        }

        MapChromeButton { glyph: Theme.dark ? "☀" : "☾"; onActivated: Theme.toggle() }
        MapChromeButton { glyph: "+"; onActivated: map.zoomLevel += 0.5 }
        MapChromeButton { glyph: "−"; onActivated: map.zoomLevel -= 0.5 }
        MapChromeButton {
            glyph: "🎯"
            onActivated: { map.center = currentPos; map.zoomLevel = 15; }
        }
    }

    // ---- Search overlay (hidden while navigating) ----
    Column {
        visible: !NavController.navigating
        anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 20
        width: Math.min(400, parent.width - 40); spacing: 5

        Rectangle {
            width: parent.width; height: 50; color: Theme.surface; radius: 25
            border.color: searchInput.activeFocus ? Theme.accent : Theme.border
            RowLayout {
                anchors.fill: parent; anchors.margins: 15
                Text { text: "🔍"; color: Theme.textSecondary; font.pixelSize: 18 }

                TextField {
                    id: searchInput
                    Layout.fillWidth: true; background: null; color: Theme.textPrimary; font.pixelSize: 16
                    placeholderText: "Where to?"; placeholderTextColor: Theme.textFaint

                    onTextChanged: {
                        if (text.length > 2) searchDebouncer.restart();
                        else searchDebouncer.stop();
                    }

                    onAccepted: {
                        if (text.length > 2) {
                            searchDebouncer.stop();
                            safeApiCall(text, false);
                        }
                    }
                }

                Button {
                    visible: searchInput.text !== ""
                    text: "✖"; background: null
                    contentItem: Text { text: parent.text; color: Theme.textSecondary }
                    onClicked: { searchInput.text = ""; sortedResultsModel.clear(); }
                }
            }
        }

        Timer {
            id: searchDebouncer
            interval: 800
            repeat: false
            onTriggered: { safeApiCall(searchInput.text, false); }
        }

        Rectangle {
            width: parent.width
            height: Math.min(suggestionList.contentHeight, 300)
            color: Theme.surface; radius: 15; clip: true
            visible: searchInput.activeFocus && sortedResultsModel.count > 0

            ListView {
                id: suggestionList
                anchors.fill: parent
                model: sortedResultsModel

                delegate: Item {
                    width: suggestionList.width; height: 70
                    Rectangle {
                        anchors.fill: parent; color: "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 15; spacing: 15

                            Image {
                                source: "qrc:/icons/resources/pin.svg"
                                width: 20; height: 20
                                sourceSize.width: 20; sourceSize.height: 20
                            }

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Text { text: titleText; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: subtitleText; color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Text { text: formatDistanceStr(distMeters); color: Theme.accent; font.pixelSize: 14; font.bold: true }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                searchInput.focus = false;
                                var targetCoord = QtPositioning.coordinate(coordLat, coordLon);
                                droppedPin.coordinate = targetCoord;
                                droppedPin.visible = true;
                                map.center = targetCoord;
                                NavController.dropPin(targetCoord);
                                NavController.updateLocationInfo(titleText, subtitleText);
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                }
            }
        }
    }

    // ---- Turn-by-turn overlay (top-left, while navigating) ----
    Rectangle {
        id: navOverlay
        visible: NavController.navigating
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 16
        width: 280
        height: navColumn.implicitHeight + 24
        color: Theme.surface; radius: 16; border.color: Theme.border
        opacity: 0.97

        ColumnLayout {
            id: navColumn
            anchors.fill: parent; anchors.margins: 12; spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Navigate"; color: Theme.textPrimary; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
                Text { text: "🔊"; color: Theme.textFaint; font.pixelSize: 16 }
            }

            ListView {
                id: directionsList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 230)
                clip: true
                model: NavController.directions
                delegate: RowLayout {
                    required property var modelData
                    width: directionsList.width; height: 56; spacing: 12
                    Text { text: modelData.dirGlyph; color: Theme.accent; font.pixelSize: 26; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text { text: modelData.distanceText; color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
                        Text { text: modelData.instruction; color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true; maximumLineCount: 2; wrapMode: Text.WordWrap }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Button {
                Layout.fillWidth: true; Layout.preferredHeight: 40
                text: "CANCEL TRIP"
                background: Rectangle { color: Theme.surfaceAlt; radius: 8 }
                contentItem: Text { text: parent.text; color: Theme.danger; font.pixelSize: 14; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: {
                    routeQuery.clearWaypoints();
                    routeModel.reset();
                    NavController.clearRoute();
                    NavController.navigating = false;
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 2
                Text { text: NavController.routeDistanceText; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Text { text: NavController.routeDurationText; color: Theme.accent; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Text { text: NavController.routeEtaText; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    // ---- Bottom drawer: Media <-> Map bar (swipe to swap, drag to expand) ----
    Rectangle {
        id: drawer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: drawerExpanded ? expandedHeight : drawerHeight
        Behavior on height { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        color: Theme.surface
        // Full-width rectangle, rounded only on the top corners (fills the gaps).
        topLeftRadius: 18; topRightRadius: 18; bottomLeftRadius: 0; bottomRightRadius: 0

        HoverHandler { id: drawerHover }
        // Absorb stray taps; controls/pager sit above this and get clicks first.
        MouseArea { anchors.fill: parent }

        // Rotational swap: any horizontal swipe (either direction) flips panels.
        DragHandler {
            id: swapDrag
            target: null; yAxis.enabled: false; xAxis.enabled: true
            property real pressX: 0
            onActiveChanged: {
                if (active) pressX = centroid.scenePosition.x;
                else if (Math.abs(centroid.scenePosition.x - pressX) > 40)
                    NavController.panel = (NavController.panel === 0 ? 1 : 0);
            }
        }

        // --- Grey handle: drag up to expand, down to retract; tap toggles. ---
        Item {
            id: handleArea
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 22
            Rectangle { anchors.centerIn: parent; width: 46; height: 5; radius: 2.5; color: Theme.textFaint }
            TapHandler { onTapped: rightScreen.drawerExpanded = !rightScreen.drawerExpanded }
            DragHandler {
                target: null; xAxis.enabled: false; yAxis.enabled: true
                property real pressY: 0
                onActiveChanged: {
                    if (active) pressY = centroid.scenePosition.y;
                    else {
                        var dy = centroid.scenePosition.y - pressY;
                        if (dy < -20) rightScreen.drawerExpanded = true;
                        else if (dy > 20) rightScreen.drawerExpanded = false;
                    }
                }
            }
        }

        // Horizontally sliding pager holding the two panels.
        Item {
            id: pager
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: handleArea.bottom; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
            clip: true

            Row {
                width: pager.width * 2; height: pager.height
                x: -NavController.panel * pager.width
                Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

                // ================= Panel 0: MEDIA =================
                Item {
                    width: pager.width; height: pager.height

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16; anchors.rightMargin: 16
                        anchors.topMargin: 6; anchors.bottomMargin: 6
                        spacing: 8

                        // ---- Selected media (now playing) — always shown ----
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 76
                            spacing: 14

                            Image {
                                source: MediaController.albumArt
                                sourceSize.width: 56; sourceSize.height: 56
                                width: 56; height: 56
                            }

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Text {
                                    text: MediaController.trackTitle === "" ? "Not playing" : MediaController.trackTitle
                                    color: Theme.textPrimary; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Text {
                                    text: MediaController.statusText !== "" ? MediaController.statusText : MediaController.artist
                                    color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Text {
                                        visible: MediaController.isLive
                                        text: "● LIVE"; color: Theme.danger; font.pixelSize: 11; font.bold: true
                                    }
                                    Slider {
                                        visible: !MediaController.isLive
                                        Layout.fillWidth: true
                                        from: 0; to: Math.max(1, MediaController.durationSec)
                                        value: MediaController.positionSec
                                        onMoved: MediaController.seekTo(value)
                                    }
                                    Text {
                                        visible: !MediaController.isLive
                                        text: MediaController.positionText + " / " + MediaController.durationText
                                        color: Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                            }

                            component XportButton : Rectangle {
                                property string glyph: ""
                                property int size: 38
                                signal activated()
                                width: size; height: size; radius: size / 2
                                color: tap.pressed ? Theme.surfaceAlt : "transparent"
                                Text { anchors.centerIn: parent; text: glyph; color: Theme.textPrimary; font.pixelSize: 20 }
                                MouseArea { id: tap; anchors.fill: parent; onClicked: parent.activated() }
                            }

                            XportButton { glyph: "⏮"; onActivated: MediaController.previous() }
                            XportButton {
                                glyph: MediaController.playing ? "⏸" : "▶"; size: 46
                                onActivated: MediaController.togglePlay()
                            }
                            XportButton { glyph: "⏭"; onActivated: MediaController.next() }
                            XportButton {
                                glyph: rightScreen.drawerExpanded ? "⌄" : "☰"
                                onActivated: rightScreen.drawerExpanded = !rightScreen.drawerExpanded
                            }
                        }

                        // ---- Radio / Local source toggle (only when expanded) ----
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            visible: rightScreen.drawerExpanded
                            spacing: 10

                            Repeater {
                                model: [ { label: "📻 Radio", src: 0 }, { label: "💾 Local", src: 1 } ]
                                delegate: Button {
                                    required property var modelData
                                    text: modelData.label
                                    Layout.preferredHeight: 36
                                    background: Rectangle {
                                        radius: 18
                                        color: MediaController.source === modelData.src ? Theme.accent : Theme.surfaceAlt
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: MediaController.source === modelData.src ? Theme.onAccent : Theme.textSecondary
                                        font.pixelSize: 14; font.bold: true
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                        leftPadding: 12; rightPadding: 12
                                    }
                                    onClicked: MediaController.source = modelData.src
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // Mute toggles by clicking the speaker; volume bar runs right→left.
                            Text {
                                text: MediaController.muted ? "🔇" : "🔈"
                                color: Theme.textSecondary; font.pixelSize: 18
                                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: MediaController.toggleMute() }
                            }
                            Slider {
                                Layout.preferredWidth: 120
                                LayoutMirroring.enabled: true       // right-to-left fill
                                from: 0; to: 1; value: MediaController.volume
                                onMoved: MediaController.volume = value
                            }
                        }

                        // ---- Station / track list (only when expanded) ----
                        ListView {
                            id: browseList
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                            visible: rightScreen.drawerExpanded
                            model: rightScreen.browseItems
                            ScrollBar.vertical: ScrollBar { }

                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                width: browseList.width; height: 56; radius: 10
                                color: modelData.current ? Theme.surfaceMuted : "transparent"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 12
                                    Image {
                                        source: modelData.art; sourceSize.width: 36; sourceSize.height: 36
                                        width: 36; height: 36
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 1
                                        Text { text: modelData.name; color: Theme.textPrimary; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { text: modelData.subtitle; color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                    Text {
                                        visible: modelData.current && MediaController.playing
                                        text: "▶"; color: Theme.accent; font.pixelSize: 14
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (MediaController.source === 0) MediaController.playRadio(modelData.origIndex);
                                        else MediaController.playLocal(modelData.origIndex);
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: browseList.count === 0
                                width: browseList.width - 40
                                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 13
                                text: MediaController.source === 1
                                      ? "No local media found.\nConnect a USB drive / SD card with audio files."
                                      : "No stations available."
                            }
                        }
                    }
                }

                // ================= Panel 1: MAP BAR =================
                Item {
                    width: pager.width; height: pager.height

                    // (a) Local Vibe — idle state, no pin dropped.
                    ColumnLayout {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 16
                        visible: !NavController.hasPin
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "✨"; font.pixelSize: 20 }
                            Text { text: "Local Vibe"; color: Theme.textPrimary; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
                        }
                        Text { text: "Discover what's around you."; color: Theme.textSecondary; font.pixelSize: 13 }
                        Text { text: "Tap the map to drop a pin."; color: Theme.textFaint; font.pixelSize: 12 }
                    }

                    // (b) Location details — a pin is dropped / result selected.
                    RowLayout {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.topMargin: 10
                        visible: NavController.hasPin
                        spacing: 14

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text {
                                text: NavController.destName === "" ? "Selected Location" : NavController.destName
                                color: Theme.textPrimary; font.pixelSize: 18; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Text {
                                text: NavController.destAddress
                                color: Theme.textSecondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true; visible: text !== ""
                            }
                            Text {
                                text: currentPos.isValid && NavController.destCoordinate.isValid
                                      ? formatDistanceStr(currentPos.distanceTo(NavController.destCoordinate)) + " away" : ""
                                color: Theme.accent; font.pixelSize: 13; font.bold: true
                            }
                        }

                        Button {
                            text: "✖"; flat: true
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; font.pixelSize: 18 }
                            onClicked: { NavController.clearPin(); droppedPin.visible = false; }
                        }

                        Button {
                            text: "Start Navigation"
                            Layout.preferredHeight: 56; Layout.preferredWidth: 180
                            background: Rectangle { color: Theme.accent; radius: 10 }
                            contentItem: Text { text: parent.text; color: Theme.onAccent; font.pixelSize: 16; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: {
                                routeQuery.clearWaypoints();
                                routeQuery.addWaypoint(currentPos);
                                routeQuery.addWaypoint(NavController.destCoordinate);
                                routeModel.update();
                                NavController.navigating = true;
                            }
                        }
                    }
                }
            }
        }

        // Page dots (Media ● ○ Map) — indicator only; swipe to switch.
        PageIndicator {
            id: pageDots
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 3
            count: 2
            currentIndex: NavController.panel
        }
    }
}
