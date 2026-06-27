import QtQuick
import QtQuick.Window
import FluxEngine 1.0

Window {
    id: root
    width: 640; height: 540
    visible: true
    title: "Flux — C++ QRhi Plugin"
    color: "#000"

    readonly property int simSize: 128

    /* ── Debug mode: 0=Normal 1=Fluid 2=Noise 3=Pressure 4=Divergence 5=Lines ── */
    property int debugMode: 0

    /* ── Simulation via C++ QRhi Plugin (self-rendered via QSGGeometryNode) ── */
    FluxBackground {
        id: sim
        anchors.fill: parent
        debugMode: root.debugMode
        running: true
    }

    /* ── Debug mode buttons ── */
    Row {
        id: modeBar
        anchors {
            top: parent.top; topMargin: 12
            horizontalCenter: parent.horizontalCenter
        }
        spacing: 6

        Repeater {
            model: ["Normal", "Fluid", "Noise", "Pressure", "Divergence", "Lines"]
            delegate: Rectangle {
                width: 96; height: 28
                radius: 4
                color: root.debugMode === index ? "#3a7bd5" : "#1e1e30"
                border.color: root.debugMode === index ? "#5a9bf5" : "#333"

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: root.debugMode === index ? "#fff" : "#888"
                    font.pixelSize: 11
                    font.bold: root.debugMode === index
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.debugMode = index
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    /* ── FPS + mode label ── */
    Text {
        id: fpsLabel
        anchors {
            bottom: parent.bottom; bottomMargin: 8
            horizontalCenter: parent.horizontalCenter
        }
        color: "#666"
        text: "Flux C++ · 128×128 · waiting..."
        font.pixelSize: 11
    }

    property int _prevFrames: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: {
            var fps = sim.frameCount - root._prevFrames;
            root._prevFrames = sim.frameCount;
            var modeNames = ["Normal", "Fluid", "Noise", "Pressure", "Divergence", "Lines"];
            fpsLabel.text = "Flux C++ · 128×128 · mode=%1 · %2 fps"
                .arg(modeNames[root.debugMode]).arg(fps);
        }
    }

    /* ── Grab frames for automated analysis ── */
    Timer {
        interval: 3000; running: true; repeat: false
        onTriggered: { root.grabToImage(function(r) { r.saveToFile("/tmp/flux_cpp_3s.png"); }); }
    }
    Timer {
        interval: 8000; running: true; repeat: false
        onTriggered: {
            root.grabToImage(function(r) { r.saveToFile("/tmp/flux_cpp_8s.png"); });
        }
    }
    Timer {
        interval: 12000; running: true; repeat: false
        onTriggered: { root.debugMode = 1; }
    }
    Timer {
        interval: 13500; running: true; repeat: false
        onTriggered: {
            root.grabToImage(function(r) { r.saveToFile("/tmp/flux_cpp_debug_fluid.png"); });
        }
    }
    Timer {
        interval: 14500; running: true; repeat: false
        onTriggered: { root.debugMode = 4; }
    }
    Timer {
        interval: 16500; running: true; repeat: false
        onTriggered: {
            root.grabToImage(function(r) { r.saveToFile("/tmp/flux_cpp_debug_divergence.png"); });
        }
    }
    Timer {
        interval: 15500; running: true; repeat: false
        onTriggered: { root.debugMode = 5; }
    }
    Timer {
        interval: 17000; running: true; repeat: false
        onTriggered: {
            root.grabToImage(function(r) { r.saveToFile("/tmp/flux_cpp_debug_lines.png"); });
        }
    }
    Timer {
        interval: 22000; running: true; repeat: false
        onTriggered: { Qt.quit(); }
    }
}
