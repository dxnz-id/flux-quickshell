import QtQuick
import QtQuick.Window

Window {
    width: 600; height: 460
    visible: true
    title: "Flux — Pressure 19 Test"
    color: "#1a1a2e"

    readonly property int simSize: 128

    FluxSimulation {
        id: sim
        simSize: root.simSize
        running: true
    }

    Rectangle {
        id: displayArea
        anchors.centerIn: parent
        y: 20
        width: simSize * 3; height: simSize * 3
        color: "black"
        border { color: "#333"; width: 1 }
        ShaderEffect {
            anchors.fill: parent
            property var simTex: sim.output
            fragmentShader: "shaders/visualize.qsb"
        }
    }

    Text {
        id: fpsLabel
        anchors { bottom: parent.bottom; bottomMargin: 8; horizontalCenter: parent.horizontalCenter }
        color: "#888"
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
            fpsLabel.text = "Flux · 128×128 · 19 press iters · %1 fps".arg(fps);
        }
    }

    /* ── Grab frames for analysis ── */
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
        interval: 11000; running: true; repeat: false
        onTriggered: {
            // Save final FPS value as a tiny image for verification
            var fps = sim.frameCount - root._prevFrames;
            fpsLabel.text = "FPS_END:" + fps;
        }
    }
    Timer {
        interval: 13000; running: true; repeat: false
        onTriggered: {
            displayArea.grabToImage(function(r) { r.saveToFile("/tmp/flux_19iter_13s.png"); });
            Qt.quit();
        }
    }
}
