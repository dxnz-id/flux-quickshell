#version 440

layout(binding = 0) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

ivec2 clampPos(ivec2 p, ivec2 size) {
    return clamp(p, ivec2(0), size - ivec2(1));
}

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(velocityTex, 0);
    vec2 c = texelFetch(velocityTex, pos, 0).xy;
    vec2 l = texelFetch(velocityTex, clampPos(pos + ivec2(-1, 0), sz), 0).xy;
    vec2 r = texelFetch(velocityTex, clampPos(pos + ivec2( 1, 0), sz), 0).xy;
    vec2 b = texelFetch(velocityTex, clampPos(pos + ivec2(0, -1), sz), 0).xy;
    vec2 t = texelFetch(velocityTex, clampPos(pos + ivec2(0,  1), sz), 0).xy;
    // at boundary: clamped neighbor = self → diff = 0 → no-slip (zero gradient)
    // but next pass applies explicit no-slip via subtract_gradient
    float stencil_factor = 0.0625f;
    float center_factor = 12.0f;
    vec2 newVel = stencil_factor * (l + r + b + t + center_factor * c);
    fragColor = vec4(newVel, 0.0, 1.0);
}
