import FluxEngine 1.0
import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root
    width: 900; height: 750
    visible: true
    title: "FluxEngine — Debug Visualizer"
    color: "#000"

    readonly property int simSize: 128

    FluxItem {
        id: sim
        anchors {
            fill: parent
            bottomMargin: 44  /* space for buttons */
        }
        simSize: root.simSize
        running: true
        debugMode: modeBar.selected
        diagStep: 5
    }

    Timer {
        interval: 16
        running: true
        repeat: true
        onTriggered: sim.onFrameTick()
    }

    /* ── Debug mode buttons ── */
    Row {
        id: modeBar
        anchors {
            bottom: parent.bottom; bottomMargin: 8
            horizontalCenter: parent.horizontalCenter
        }
        spacing: 6
        property int selected: 5  // Lines

        Repeater {
            model: ["Normal", "Noise", "Fluid", "Pressure", "Divergence", "Lines"]

            delegate: Rectangle {
                width: 96; height: 28; radius: 4
                color: modeBar.selected === index ? "#3a7bd5" : "#1e1e30"
                border.color: modeBar.selected === index ? "#5a9bf5" : "#333"

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: modeBar.selected === index ? "#fff" : "#888"
                    font.pixelSize: 11; font.bold: modeBar.selected === index
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: modeBar.selected = index
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    /* ── FPS + mode label ── */
    property int _prevFrames: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: {
            var fps = sim.frameCount - root._prevFrames;
            root._prevFrames = sim.frameCount;
            var modeNames = ["Normal", "Noise", "Fluid", "Pressure", "Divergence", "Lines"];
            console.log("Flux · %1×%2 · mode=%3 · %4 fps"
                .arg(root.simSize).arg(root.simSize)
                .arg(modeNames[modeBar.selected]).arg(fps));
        }
    }

    /* ── Grab for manual check ── */
    Timer {
        interval: 5000; running: true; repeat: false
        onTriggered: {
            sim.grabToImage(function(r) { r.saveToFile("/tmp/flux_sandbox_grab.png");
                console.log("GRAB saved: " + r.size().width + "x" + r.size().height); });
        }
    }
}
