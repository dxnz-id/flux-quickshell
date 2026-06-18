#version 420

layout(binding = 0) uniform sampler2D lineDataTexture;

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord0;
layout(location = 1) out vec4 lineColor;

layout(std140, binding = 1) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    // Read texels via normalized UV (no texelFetch)
    vec4 t0 = texture(lineDataTexture, vec2(0.25, 0.5));
    vec4 t1 = texture(lineDataTexture, vec2(0.75, 0.5));

    vec2 endpoint = t0.xy;
    lineColor = vec4(t1.rgb, 1.0);

    // Position quad at endpoint * 200 (center of 200×200 ShaderEffect at 0.5)
    vec2 pos = qt_Vertex.xy * 0.1 + endpoint * 200.0;

    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * vec4(pos, 0.0, 1.0);
}
