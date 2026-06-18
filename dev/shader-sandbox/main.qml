import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 640; height: 540
    visible: true
    title: "Flux — Debug Visualizer"
    color: "#0d0d1a"

    readonly property int simSize: 128

    /* ── Debug mode: 0=Normal 1=Fluid 2=Noise 3=Pressure 4=Divergence 5=Lines ── */
    property int debugMode: 0

    FluxSimulation {
        id: sim
        simSize: root.simSize
        running: true
    }

    /* ── Simulation display ── */
    Rectangle {
        id: displayArea
        anchors.horizontalCenter: parent.horizontalCenter
        y: 20
        width: simSize * 3; height: simSize * 3
        color: "black"
        border { color: "#333"; width: 1 }
        ShaderEffect {
            anchors.fill: parent
            /* mode 5 = Lines: switch simTex to sim.lines; else use velocity field */
            property var simTex: root.debugMode === 5 ? sim.lines : sim.output
            property int debugMode: root.debugMode
            fragmentShader: "shaders/visualize.qsb"
        }
    }

    /* ── Debug mode buttons ── */
    Row {
        id: modeBar
        anchors {
            top: displayArea.bottom
            topMargin: 12
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
        text: "Flux · 128×128 · 19 press iters · waiting..."
        font.pixelSize: 11
    }

    /* ── FPS counter ── */
    property int _prevFrames: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: {
            var fps = sim.frameCount - root._prevFrames;
            root._prevFrames = sim.frameCount;
            var modeNames = ["Normal", "Fluid", "Noise", "Pressure", "Divergence", "Lines"];
            fpsLabel.text = "Flux · 128×128 · mode=%1 · %2 fps"
                .arg(modeNames[root.debugMode]).arg(fps);
        }
    }

    /* ── Grab frames for automated analysis (headless testing) ── */
    Timer {
        interval: 3000; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_19iter_3s.png"); });
        }
    }
    Timer {
        interval: 8000; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_19iter_8s.png"); });
        }
    }
    Timer {
        interval: 12000; running: true; repeat: false
        onTriggered: {
            // Switch to fluid debug mode and capture
            root.debugMode = 1;
        }
    }
    Timer {
        interval: 13500; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_debug_fluid.png"); });
        }
    }
    Timer {
        interval: 14500; running: true; repeat: false
        onTriggered: {
            root.debugMode = 4;  // Switch to divergence
        }
    }
    Timer {
        interval: 16500; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_debug_divergence.png"); });
        }
    }
    Timer {
        interval: 15500; running: true; repeat: false
        onTriggered: { root.debugMode = 5; }  // Switch to Lines
    }
    Timer {
        interval: 17000; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_debug_lines.png"); });
        }
    }
    Timer {
        interval: 22000; running: true; repeat: false
        onTriggered: {
            Qt.quit();
        }
    }
}
