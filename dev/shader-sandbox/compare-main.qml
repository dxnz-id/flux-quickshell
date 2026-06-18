import QtQuick
import QtQuick.Window

Window {
    width: 1000; height: 500
    visible: true
    color: "black"
    title: "Heatmap vs Flow Lines Comparison"

    FluxSimulation {
        id: sim
        simSize: 128
        running: true
    }

    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: "black"

            Column {
                anchors.fill: parent
                Text {
                    width: parent.width
                    color: "white"; text: "Heatmap (visualize.frag)"
                    font.pixelSize: 12; horizontalAlignment: Text.Center
                }
                ShaderEffect {
                    width: parent.width
                    height: parent.height - 20
                    property var simTex: sim.output
                    fragmentShader: "shaders/visualize.qsb"
                }
            }
        }

        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: "black"

            Column {
                anchors.fill: parent
                Text {
                    width: parent.width
                    color: "white"; text: "Flow Lines (flow_lines.frag)"
                    font.pixelSize: 12; horizontalAlignment: Text.Center
                }
                ShaderEffect {
                    width: parent.width
                    height: parent.height - 20
                    property var simTex: sim.output
                    fragmentShader: "shaders/flow_lines.qsb"
                }
            }
        }
    }

    Timer {
        interval: 8000; running: true; repeat: false
        onTriggered: Qt.quit()
    }
}
