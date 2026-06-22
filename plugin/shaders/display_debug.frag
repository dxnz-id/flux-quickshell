#version 440
layout(binding = 0) uniform sampler2D uTex;
layout(location = 0) out vec4 fragColor;

const int DISPLAY_SIZE = 256;

void main() {
    ivec2 srcSize = textureSize(uTex, 0);
    ivec2 dstPos = ivec2(gl_FragCoord.xy);
    ivec2 srcPos = dstPos * srcSize / DISPLAY_SIZE;
    vec3 c = 0.5 + 0.5 * texelFetch(uTex, srcPos, 0).rgb;
    float contrast = 2.0;
    c = clamp(contrast * (c - 0.5) + 0.5, 0.0, 1.0);
    fragColor = vec4(c, 1.0);
}
