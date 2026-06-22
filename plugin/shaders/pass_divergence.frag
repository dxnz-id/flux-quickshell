#version 440

layout(binding = 0) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

ivec2 clampPos(ivec2 p, ivec2 size) {
    return clamp(p, ivec2(0), size - ivec2(1));
}

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(velocityTex, 0);
    float l = texelFetch(velocityTex, clampPos(pos + ivec2(-1, 0), sz), 0).x;
    float r = texelFetch(velocityTex, clampPos(pos + ivec2( 1, 0), sz), 0).x;
    float t = texelFetch(velocityTex, clampPos(pos + ivec2(0,  1), sz), 0).y;
    float b = texelFetch(velocityTex, clampPos(pos + ivec2(0, -1), sz), 0).y;
    float newDiv = 0.5 * ((r - l) + (t - b));
    fragColor = vec4(newDiv, 0.0, 0.0, 1.0);
}
