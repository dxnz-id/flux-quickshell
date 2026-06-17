#version 420

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 v = (uv - 0.5) * 2.0;
    // RG = velocity (bias), B = 0.0 initialized (bias=0.5), A = 1.0
    fragColor = vec4(v * 0.5 + 0.5, 0.5, 1.0);
}
