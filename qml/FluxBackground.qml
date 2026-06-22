import QtQuick
import FluidSim 1.0

Item {
    id: root
    property bool running: true
    property alias frameCount: sim.frameCount

    FluidSimItem {
        id: sim
        anchors.fill: parent
        simSize: 128
        running: root.running
    }
}
