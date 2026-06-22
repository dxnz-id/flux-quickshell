#version 440
layout(binding = 0) uniform sampler2D uTex;
layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 ts = textureSize(uTex, 0);
    vec2 uv = gl_FragCoord.xy / vec2(ts);
    vec3 c = 0.5 + 0.5 * texture(uTex, uv).rgb;
    float contrast = 2.0;
    c = clamp(contrast * (c - 0.5) + 0.5, 0.0, 1.0);
    fragColor = vec4(c, 1.0);
}
