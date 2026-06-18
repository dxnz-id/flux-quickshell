import Quickshell
import QtQuick

ShellRoot {
    PanelWindow {
        anchors.top: true
        anchors.left: true
        implicitWidth: 200
        implicitHeight: 200
        color: "black"

        ShaderEffect {
            anchors.fill: parent
            vertexShader: "../../shaders/compiled/test_vertex.qsb"
            fragmentShader: "../../shaders/compiled/test_vertex_frag.qsb"
        }
    }
}
