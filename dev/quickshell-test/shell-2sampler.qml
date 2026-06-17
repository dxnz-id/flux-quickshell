import Quickshell
import QtQuick

// 2-sampler test: if binding 1 works, left half = blue, right half = green.
// If binding 1 is bugged (Qt 6.11 multi-sampler bug), both halves = same texture.

ShellRoot {
    id: root

    Rectangle { id: src1; width: 1; height: 1; visible: false; color: Qt.rgba(0.0, 0.0, 1.0, 1.0) }
    ShaderEffectSource { id: tex1; sourceItem: src1; live: true; hideSource: true }

    Rectangle { id: src2; width: 1; height: 1; visible: false; color: Qt.rgba(0.0, 1.0, 0.0, 1.0) }
    ShaderEffectSource { id: tex2; sourceItem: src2; live: true; hideSource: true }

    PanelWindow {
        anchors.top: true; anchors.left: true
        implicitWidth: 350; implicitHeight: 220
        color: "#1a1a2e"

        Row {
            anchors.centerIn: parent
            spacing: 30

            Column {
                spacing: 4
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: "#aaa"; text: "1 sampler (passthrough)"; font.pixelSize: 10 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: "#666"; text: "expected: blue"; font.pixelSize: 9 }
                ShaderEffect {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 100; height: 100
                    property var simTex: tex1
                    fragmentShader: "../../shaders/compiled/passthrough.qsb"
                }
            }

            Column {
                spacing: 4
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: "#aaa"; text: "2 samplers (switch_test)"; font.pixelSize: 10 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: "#666"; text: "expected: blue|white|green"; font.pixelSize: 9 }
                ShaderEffect {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 100; height: 100
                    property var texA: tex1
                    property var texB: tex2
                    fragmentShader: "../../shaders/compiled/switch_test.qsb"
                }
            }
        }
    }
}
