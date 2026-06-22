import FluidSim 1.0
import QtQuick 2.15

Rectangle {
    width: 900
    height: 700
    color: "#222"

    FluidSimItem {
        id: sim
        anchors.fill: parent
        simSize: 128
        running: true
    }

    Text {
        anchors { bottom: parent.bottom; right: parent.right; margins: 8 }
        color: "white"
        font.pixelSize: 16
        text: "Frame: " + sim.frameCount
    }
}
