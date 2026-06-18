#version 420
layout(binding = 0) uniform sampler2D texA;
layout(binding = 1) uniform sampler2D texB;
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 a = texture(texA, qt_TexCoord0);
    vec4 b = texture(texB, qt_TexCoord0);
    // R = texA.r (expected 1.0 for red)
    // G = texB.b (expected 1.0 if binding 1 reads blue texture correctly)
    // B = 0
    fragColor = vec4(a.r, b.b, 0.0, 1.0);
}
