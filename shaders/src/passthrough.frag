#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(simTex, qt_TexCoord0);
}
