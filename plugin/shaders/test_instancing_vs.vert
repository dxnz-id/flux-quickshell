#version 440
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inOffset;
layout(location = 0) out vec4 fragColor;
void main() {
    gl_Position = vec4(inPos + inOffset, 0.0, 1.0);
    fragColor = inColor;
}
