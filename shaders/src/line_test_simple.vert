#version 420

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord0;
layout(location = 1) out vec4 lineColor;

layout(std140, binding = 1) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    lineColor = vec4(0.0, 1.0, 0.0, 1.0);  // green, no texture read
    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * vec4(qt_Vertex.xy * 0.1 + 50.0, 0.0, 1.0);
}
