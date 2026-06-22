#version 440

layout(binding = 0) uniform sampler2D forwardTex;
layout(binding = 1) uniform sampler2D reverseTex;
layout(binding = 2) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(velocityTex, 0);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 forward = texelFetch(forwardTex, pos, 0).xy;
    vec2 reverse = texelFetch(reverseTex, pos, 0).xy;

    // Backtrack to find clamp region in the flow direction
    // Reference: advected_position = (pos + 1.0) - timestep * velocity
    float timestep = 0.016667f;
    ivec2 srcCell = ivec2(floor(vec2(pos) + 1.0 - timestep * velocity));
    srcCell = clamp(srcCell, ivec2(1), size - 2);

    vec2 l = texelFetch(velocityTex, srcCell + ivec2(-1, 0), 0).xy;
    vec2 r = texelFetch(velocityTex, srcCell + ivec2( 1, 0), 0).xy;
    vec2 b = texelFetch(velocityTex, srcCell + ivec2(0, -1), 0).xy;
    vec2 t = texelFetch(velocityTex, srcCell + ivec2(0,  1), 0).xy;

    vec2 minVel = min(min(l, r), min(b, t));
    vec2 maxVel = max(max(l, r), max(b, t));

    vec2 adjusted = forward + 0.5 * (velocity - reverse);
    vec2 newVel = clamp(adjusted, minVel, maxVel);

    fragColor = vec4(newVel, 0.0, 0.0);
}
