#version 440
layout(binding = 0) uniform sampler2D velTex;
layout(location = 0) out vec4 fragColor;

// Jet colormap from velocity magnitude
vec3 jet(float v) {
    v = clamp(v, 0.0, 1.0);
    if (v < 0.125) return mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.0, 1.0), v / 0.125);
    if (v < 0.375) return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), (v - 0.125) / 0.25);
    if (v < 0.625) return mix(vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), (v - 0.375) / 0.25);
    if (v < 0.875) return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (v - 0.625) / 0.25);
    return mix(vec3(1.0, 0.0, 0.0), vec3(0.5, 0.0, 0.0), (v - 0.875) / 0.125);
}

void main() {
    // Display is 256x256, velocity texture is 128x128 — scale by 0.5
    vec2 vel = texelFetch(velTex, ivec2(gl_FragCoord.xy * 0.5), 0).xy;
    float speed = length(vel) * 4.0; // scale for visibility
    vec3 color = jet(speed);
    fragColor = vec4(color, 1.0);
}
