#version 440
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    if ((coord.x / 4 + coord.y / 4) % 2 == 0)
        fragColor = vec4(1.0, 0.0, 0.0, 1.0);
    else
        fragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
