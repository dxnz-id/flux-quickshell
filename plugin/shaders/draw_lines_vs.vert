#version 440
layout(location = 0) in vec2 aVertex;
layout(location = 0) out vec2 vPos;
void main() {
    gl_Position = vec4(aVertex, 0.0, 1.0);
    vPos = aVertex;
}
