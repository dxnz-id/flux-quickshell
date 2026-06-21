import QtQuick
import FluidSim

Rectangle {
    width: 600; height: 600; color: "#222"

    FluidSimItem {
        id: sim
        anchors.fill: parent
        simSize: 128
        running: true
    }

    Text {
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        color: "white"; font.pixelSize: 18
        text: "Frame: " + sim.frameCount
    }
}
