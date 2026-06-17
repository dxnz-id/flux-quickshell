import Quickshell
import QtQuick

// Minimal test: verify ShaderEffect with 1 sampler works inside Quickshell.
// If this renders a colored gradient, the 1-sampler pattern from sandbox
// is compatible with Quickshell runtime.

ShellRoot {
    id: root

    // 1×1 colored source (blue gradient: R=0.0, G=0.5, B=1.0)
    Rectangle {
        id: srcRect
        width: 1; height: 1; visible: false
        color: Qt.rgba(0.0, 0.5, 1.0, 1.0)
    }
    ShaderEffectSource {
        id: srcTex
        sourceItem: srcRect
        live: true; hideSource: true
    }

    // ── Test window ──
    PanelWindow {
        anchors.top: true; anchors.left: true
        implicitWidth: 400; implicitHeight: 250
        color: "#1a1a2e"

        Column {
            anchors.centerIn: parent
            spacing: 8

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#aaa"; text: "1 sampler → passthrough.qsb"; font.pixelSize: 11
            }

            // Test: 1 sampler ShaderEffect
            ShaderEffect {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 128; height: 128
                property var simTex: srcTex
                fragmentShader: "../../shaders/compiled/passthrough.qsb"
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#aaa"; text: "1 sampler → visualize.qsb"; font.pixelSize: 11
            }

            // Test: visualize with same 1×1 source
            ShaderEffect {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 128; height: 128
                property var simTex: srcTex
                fragmentShader: "../../shaders/compiled/visualize.qsb"
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#888"
                text: "(expected: blue square above, colored gradient below)"
                font.pixelSize: 10
            }
        }
    }
}
