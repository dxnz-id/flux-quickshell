import QtQuick
import QtQuick.Window

Window {
    width: 1050; height: 350
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

    // ── Pass 3: Forward Advection (semi-Lagrangian) ──
    ShaderEffect {
        id: passAdvect
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcSubtract
        fragmentShader: "shaders/advect_forward.qsb"
    }
    ShaderEffectSource {
        id: srcAdvect
        sourceItem: passAdvect
        live: true; hideSource: true
    }

    // ── Passes 4-6: Diffusion (Jacobi, 3 chain) ──
    ShaderEffect {
        id: passDiffuse1
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcAdvect
        fragmentShader: "shaders/diffuse.qsb"
    }
    ShaderEffectSource {
        id: srcDiffuse1
        sourceItem: passDiffuse1
        live: true; hideSource: true
    }
    ShaderEffect {
        id: passDiffuse2
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcDiffuse1
        fragmentShader: "shaders/diffuse.qsb"
    }
    ShaderEffectSource {
        id: srcDiffuse2
        sourceItem: passDiffuse2
        live: true; hideSource: true
    }
    ShaderEffect {
        id: passDiffuse3
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcDiffuse2
        fragmentShader: "shaders/diffuse.qsb"
    }
    ShaderEffectSource {
        id: srcDiffuse3
        sourceItem: passDiffuse3
        live: true; hideSource: true
    }

    // ── Pass 7: Noise injection ──
    ShaderEffect {
        id: passNoise
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcDiffuse3
        fragmentShader: "shaders/noise.qsb"
    }
    ShaderEffectSource {
        id: srcNoise
        sourceItem: passNoise
        live: true; hideSource: true
    }

    // ── Display (passthrough ShaderEffect, bukan Image) ──
    Row {
        anchors.centerIn: parent
        spacing: 8
        Column {
            Text { color: "white"; text: "Initial Velocity (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcInit
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "After ∇· + ∇p (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcPressure
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Vel - ∇P (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcSubtract
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Advected (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcAdvect
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Diffuse 1 (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcDiffuse1
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Diffuse 2 (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcDiffuse2
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Diffuse 3 (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcDiffuse3
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
        Column {
            Text { color: "white"; text: "Noise (RG)"; font.pixelSize: 11 }
            ShaderEffect {
                width: simSize; height: simSize
                property var simTex: srcNoise
                fragmentShader: "shaders/passthrough.qsb"
            }
        }
    }

    // ── Test: 1×1 dynamic time via Rectangle (bukan ShaderEffect) ──
    property real t: 0.0
    Timer {
        interval: 40; running: true; repeat: true
        onTriggered: { t = (t + 0.02); if (t > 1.0) t -= 1.0; }
    }
    Rectangle {
        id: timeRect
        width: 1; height: 1
        visible: false
        color: Qt.rgba(t, t, t, 1.0)
    }
    ShaderEffectSource {
        id: timeSource
        sourceItem: timeRect
        live: true; hideSource: true
    }
    ShaderEffect {
        id: timeTestDisp
        x: 300; y: 280
        width: 40; height: 40
        property var simTex: timeSource
        fragmentShader: "shaders/test_time.qsb"
    }
    Text {
        x: 300; y: 268; color: "white"
        text: "1x1 time test:"; font.pixelSize: 10
    }

    Timer {
        interval: 3000; running: true; repeat: false
        onTriggered: {
            passInit.grabToImage(function(r) { r.saveToFile("/tmp/flux_velocity_input.png"); });
            passPressure.grabToImage(function(r) { r.saveToFile("/tmp/flux_pressure.png"); });
            passSubtract.grabToImage(function(r) { r.saveToFile("/tmp/flux_final_output.png"); });
            passAdvect.grabToImage(function(r) { r.saveToFile("/tmp/flux_advected.png"); });
            passDiffuse1.grabToImage(function(r) { r.saveToFile("/tmp/flux_diffuse1.png"); });
            passDiffuse2.grabToImage(function(r) { r.saveToFile("/tmp/flux_diffuse2.png"); });
            passDiffuse3.grabToImage(function(r) { r.saveToFile("/tmp/flux_diffuse3.png"); });
            passNoise.grabToImage(function(r) { r.saveToFile("/tmp/flux_noise.png"); });
            timeTestDisp.grabToImage(function(r) { r.saveToFile("/tmp/flux_time_test.png"); });
        }
    }
}
