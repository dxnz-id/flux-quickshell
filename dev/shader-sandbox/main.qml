import QtQuick
import QtQuick.Window

Window {
    width: 700; height: 350
    visible: true
    title: "Flux Pipeline — Single-Texture Channel-Packed"
    color: "black"

    readonly property int simSize: 128

    // ── Pass 0: Init Velocity + Pressure=0 ──
    ShaderEffect {
        id: passInit
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        fragmentShader: "shaders/init_velocity.qsb"
    }
    ShaderEffectSource {
        id: srcInit
        sourceItem: passInit
        live: true; hideSource: true
    }

    // ── Pass 1: Divergence + Pressure Solver (combined, 1 Jacobi) ──
    ShaderEffect {
        id: passPressure
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcInit
        fragmentShader: "shaders/divergence.qsb"
    }
    ShaderEffectSource {
        id: srcPressure
        sourceItem: passPressure
        live: true; hideSource: true
    }

    // ── Pass 2: Subtract Gradient ──
    ShaderEffect {
        id: passSubtract
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcPressure
        fragmentShader: "shaders/subtract_gradient.qsb"
    }
    ShaderEffectSource {
        id: srcSubtract
        sourceItem: passSubtract
        live: true; hideSource: true
    }

    // ── Display ──
    Row {
        anchors.centerIn: parent
        spacing: 8
        Column {
            Text { color: "white"; text: "Initial Velocity (RG)"; font.pixelSize: 11 }
            Image { source: srcInit; width: simSize; height: simSize }
        }
        Column {
            Text { color: "white"; text: "After ∇· + ∇p (RG)"; font.pixelSize: 11 }
            Image { source: srcPressure; width: simSize; height: simSize }
        }
        Column {
            Text { color: "white"; text: "Vel - ∇P (RG)"; font.pixelSize: 11 }
            Image { source: srcSubtract; width: simSize; height: simSize }
        }
    }

    Timer {
        interval: 3000; running: true; repeat: false
        onTriggered: {
            passInit.grabToImage(function(r) { r.saveToFile("/tmp/flux_velocity_input.png"); });
            passPressure.grabToImage(function(r) { r.saveToFile("/tmp/flux_pressure.png"); });
            passSubtract.grabToImage(function(r) { r.saveToFile("/tmp/flux_final_output.png"); });
        }
    }
}
