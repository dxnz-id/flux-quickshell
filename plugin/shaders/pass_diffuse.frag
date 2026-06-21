#version 440

layout(binding = 0) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    vec2 c = texelFetch(velocityTex, pos, 0).xy;
    vec2 l = texelFetch(velocityTex, pos + ivec2(-1, 0), 0).xy;
    vec2 r = texelFetch(velocityTex, pos + ivec2(1, 0), 0).xy;
    vec2 b = texelFetch(velocityTex, pos + ivec2(0, -1), 0).xy;
    vec2 t = texelFetch(velocityTex, pos + ivec2(0, 1), 0).xy;
    float stencil_factor = 0.249954f;
    float center_factor = 0.000183f;
    vec2 newVel = stencil_factor * (l + r + b + t + center_factor * c);
    fragColor = vec4(newVel, 0.0, 1.0);
}
