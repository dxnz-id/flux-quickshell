import QtQuick
import Quickshell
import Quickshell.Wayland

// flux-quickshell — Flux fluid screensaver as Wayland wallpaper
// Run with: quickshell -p qml/shell.qml
//
// Architecture:
//   FluxSimulation: hidden at 128×128 inside each PanelWindow
//   Full-screen ShaderEffect: renders flow lines at NATIVE screen resolution
//     (reads 128×128 velocity field via UV, crisp at any resolution)

ShellRoot {
    id: root

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: bgWin
            required property var modelData

            screen: modelData
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Bottom
            WlrLayershell.namespace: "quickshell:flux"

            anchors {
                top: true
                left: true
                right: true
                bottom: true
            }

            color: "black"

            /* ── Fluid simulation (ShaderEffects at x:-200,y:-200) ── */
            FluxSimulation {
                id: sim
                simSize: 128
                running: true
                // Individual ShaderEffects inside are positioned off-screen;
                // parent must stay visible so children are rendered.
            }

            /* ── Flow lines at native screen resolution ── */
            ShaderEffect {
                anchors.fill: parent
                property var simTex: sim.output   // 128×128 velocity field
                fragmentShader: "shaders/flow_lines.qsb"
            }
        }
    }
}
