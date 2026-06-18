import QtQuick

Item {
    id: root

    property int simSize: 128
    property bool running: false
    // Pressure iterations: ditentukan oleh jumlah instance ShaderEffect
    // di chain di bawah (bukan parameter dinamis). Edit kode langsung
    // untuk ubah jumlah iterasi — tambah/kurang passPressN + srcPressN.

    readonly property alias output: srcFinalA  /* velocity field for debug */
    readonly property alias lines: srcFlowLines  /* rendered flow lines for display */
    readonly property alias frameCount: root._frameCount  /* incremented each simulation frame */

    /* ── Forward advection ── (reads from previous frame output) */
    ShaderEffect {
        id: passAdvect
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        layer.format: ShaderEffectSource.RGBA16F
        property var simTex: srcFinalA
        fragmentShader: "shaders/advect_forward.qsb"
    }
    ShaderEffectSource {
        id: srcAdvect
        sourceItem: passAdvect
        live: true; hideSource: true
        format: ShaderEffectSource.RGBA16F
    }

    /* ── Diffusion (Jacobi ×3) ── */
    ShaderEffect { id: passDiff1; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcAdvect; fragmentShader: "shaders/diffuse.qsb" }
    ShaderEffectSource { id: srcDiff1; sourceItem: passDiff1; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passDiff2; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcDiff1; fragmentShader: "shaders/diffuse.qsb" }
    ShaderEffectSource { id: srcDiff2; sourceItem: passDiff2; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passDiff3; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcDiff2; fragmentShader: "shaders/diffuse.qsb" }
    ShaderEffectSource { id: srcDiff3; sourceItem: passDiff3; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }

    /* ── Noise injection ── (after diffuse, before pressure — matching flux-reference order) */
    ShaderEffect {
        id: passNoise
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        property var simTex: srcDiff3
        fragmentShader: "shaders/noise.qsb"
    }
    ShaderEffectSource {
        id: srcNoise
        sourceItem: passNoise
        live: true; hideSource: true
    }

    /* ── Pressure projection (Jacobi ×19, matching flux-reference default) ── */
    // Iterasi 1-8: chain dimulai dari output noise injection (srcNoise)
    ShaderEffect { id: passPress1;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcNoise;   fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress1;  sourceItem: passPress1;  live: true; hideSource: true }
    ShaderEffect { id: passPress2;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress1;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress2;  sourceItem: passPress2;  live: true; hideSource: true }
    ShaderEffect { id: passPress3;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress2;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress3;  sourceItem: passPress3;  live: true; hideSource: true }
    ShaderEffect { id: passPress4;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress3;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress4;  sourceItem: passPress4;  live: true; hideSource: true }
    ShaderEffect { id: passPress5;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress4;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress5;  sourceItem: passPress5;  live: true; hideSource: true }
    ShaderEffect { id: passPress6;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress5;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress6;  sourceItem: passPress6;  live: true; hideSource: true }
    ShaderEffect { id: passPress7;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress6;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress7;  sourceItem: passPress7;  live: true; hideSource: true }
    ShaderEffect { id: passPress8;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress7;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress8;  sourceItem: passPress8;  live: true; hideSource: true }
    // Iterasi 9-14
    ShaderEffect { id: passPress9;  x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress8;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress9;  sourceItem: passPress9;  live: true; hideSource: true }
    ShaderEffect { id: passPress10; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress9;  fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress10; sourceItem: passPress10; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress11; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress10; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress11; sourceItem: passPress11; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress12; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress11; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress12; sourceItem: passPress12; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress13; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress12; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress13; sourceItem: passPress13; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress14; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress13; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress14; sourceItem: passPress14; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    // Iterasi 15-19
    ShaderEffect { id: passPress15; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress14; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress15; sourceItem: passPress15; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress16; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress15; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress16; sourceItem: passPress16; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress17; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress16; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress17; sourceItem: passPress17; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress18; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress17; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress18; sourceItem: passPress18; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }
    ShaderEffect { id: passPress19; x: -200; y: -200; width: simSize; height: simSize; visible: true; opacity: 0.999; layer.enabled: true; layer.format: ShaderEffectSource.RGBA16F; property var simTex: srcPress18; fragmentShader: "shaders/divergence.qsb" }
    ShaderEffectSource { id: srcPress19; sourceItem: passPress19; live: true; hideSource: true; format: ShaderEffectSource.RGBA16F }

    /* ── Subtract pressure gradient ── */
    ShaderEffect {
        id: passSubtract
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        layer.format: ShaderEffectSource.RGBA16F
        property var simTex: srcPress19
        fragmentShader: "shaders/subtract_gradient.qsb"
    }

    /* ── Dual loopback (ping-pong: change object reference each frame) ── */
    ShaderEffectSource {
        id: srcFinalA
        sourceItem: passSubtract
        live: true; hideSource: true
        format: ShaderEffectSource.RGBA16F
    }
    ShaderEffectSource {
        id: srcFinalB
        sourceItem: passSubtract
        live: true; hideSource: true
        format: ShaderEffectSource.RGBA16F
    }

    /* ── Flow lines final render ── */
    ShaderEffect {
        id: passFlowLines
        x: -200; y: -200
        width: simSize; height: simSize
        visible: true; opacity: 0.999
        layer.enabled: true
        layer.format: ShaderEffectSource.RGBA16F
        property var simTex: srcFinalA
        fragmentShader: "shaders/flow_lines.qsb"
    }
    ShaderEffectSource {
        id: srcFlowLines
        sourceItem: passFlowLines
        live: true; hideSource: true
        format: ShaderEffectSource.RGBA16F
    }

    /* ── Per-frame trigger: FrameAnimation = vsync-locked, no timer drift ── */
    property bool _pingA: true
    property int _frameCount: 0
    FrameAnimation {
        running: root.running
        onTriggered: {
            passAdvect.simTex = root._pingA ? srcFinalA : srcFinalB
            root._pingA = !root._pingA
            root._frameCount++
        }
    }
}
