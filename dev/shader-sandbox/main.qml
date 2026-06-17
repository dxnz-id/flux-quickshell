import QtQuick
import QtQuick.Window

Window {
    width: 600; height: 400
    visible: true
    title: "Flux Simulation — Navier-Stokes"
    color: "#1a1a2e"

    readonly property int simSize: 128

    FluxSimulation {
        id: sim
        simSize: root.simSize
        pressIterations: 8
        running: true
    }

    Rectangle {
        anchors.centerIn: parent
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
        anchors { bottom: parent.bottom; bottomMargin: 8; horizontalCenter: parent.horizontalCenter }
        color: "#888"
        text: "Flux · %1×%1 · %2 press iters · %3fps"
            .arg(simSize).arg(sim.pressIterations).arg("60")
        font.pixelSize: 11
    }
}
