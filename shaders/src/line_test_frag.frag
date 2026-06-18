#version 420

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 1) in vec4 lineColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = lineColor;
}
