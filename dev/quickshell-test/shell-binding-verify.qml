import Quickshell
import QtQuick

ShellRoot {
    id: root

    // Visible color swatches as texture sources
    Rectangle { id: rectA; width: 50; height: 50; color: "red" }
    Rectangle { id: rectB; width: 50; height: 50; color: "blue" }

    ShaderEffectSource { id: srcA; sourceItem: rectA; live: true }
    ShaderEffectSource { id: srcB; sourceItem: rectB; live: true }

    PanelWindow {
        anchors.top: true; anchors.left: true
        implicitWidth: 300; implicitHeight: 200
        color: "black"

        Text {
            anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
            color: "white"; text: "binding_verify_test — expected YELLOW if binding 1 works";
            font.pixelSize: 10
        }

        Row {
            anchors.centerIn: parent
            spacing: 10

            // Source A (red)
            Rectangle { width: 50; height: 50; color: "red" }

            // Source B (blue)
            Rectangle { width: 50; height: 50; color: "blue" }

            // TEST: 2-sampler binding verify
            ShaderEffect {
                width: 50; height: 50
                property var texA: srcA
                property var texB: srcB
                fragmentShader: "../../shaders/compiled/binding_verify_test.qsb"
            }
        }
    }
}
