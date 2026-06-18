import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 400; height: 300
    visible: true
    title: "Line Test — Minimal"
    color: "#1a1a2e"

    Rectangle {
        id: display
        anchors.centerIn: parent
        width: 200; height: 200
        color: "#111"
        border { color: "#444"; width: 1 }

        // Test: vertex shader with uniform block at binding 0 (no sampler)
        ShaderEffect {
            anchors.fill: parent
            vertexShader: "../../shaders/compiled/line_test_nosampler.qsb"
            fragmentShader: "../../shaders/compiled/test_vertex_frag.qsb"
        }
    }

    Timer {
        interval: 2000; running: true; repeat: false
        onTriggered: {
            display.grabToImage(function(r) { r.saveToFile("/tmp/line_test.png"); Qt.quit(); });
        }
    }
}
