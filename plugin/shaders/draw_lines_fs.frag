#version 440
layout(binding = 0) uniform sampler2D uVel;
layout(location = 0) out vec4 fragColor;
void main() {
    vec2 vel = texelFetch(uVel, ivec2(gl_FragCoord.xy * 0.5), 0).xy;
    float speed = length(vel) * 4.0;
    fragColor = vec4(speed, 0.0, 0.0, 1.0);
}
