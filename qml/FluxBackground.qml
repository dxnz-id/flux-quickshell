import QtQuick
import FluidSim 1.0

Item {
    id: root
    property bool running: true
    property alias frameCount: sim.frameCount
    property alias debugMode: sim.debugMode
    property alias colorMode: sim.colorMode
    property alias viscosity: sim.viscosity
    property alias noiseMultiplier: sim.noiseMultiplier
    property alias timestep: sim.timestep
    property alias dissipation: sim.dissipation
    property alias pressureIterations: sim.pressureIterations
    property alias lineVariance: sim.lineVariance
    property alias lineWidthMultiplier: sim.lineWidthMultiplier
    property alias zoom: sim.zoom

    FluidSimItem {
        id: sim
        anchors.fill: parent
        simSize: 128
        running: root.running
    }

    /* ── FPS counter ── */
    property int _prevFrames: 0
    property int _fps: 0

    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: {
            _fps = sim.frameCount - _prevFrames;
            _prevFrames = sim.frameCount;
        }
    }

    Text {
        anchors { bottom: parent.bottom; left: parent.left; bottomMargin: 10; leftMargin: 10 }
        color: "white"
        font.pixelSize: 11
        text: "%1 FPS".arg(_fps)
    }
}
