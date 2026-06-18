#version 420

// NO sampler — use the same pattern as test_vertex.vert (which worked)
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord0;

void main() {
    // Scale down quad and position at center
    vec2 pos = qt_Vertex.xy * 0.3 + 70.0;
    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * vec4(pos, 0.0, 1.0);
}
