#version 420

// 2-sampler test: if binding 1 reads the CORRECT texture,
// left half = texA (blue), right half = texB (green).
// If binding 1 reads WRONG texture (the bug), both halves = same texture.

layout(binding = 0) uniform sampler2D texA;
layout(binding = 1) uniform sampler2D texB;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 colA = texture(texA, vec2(0.5, 0.5));
    vec4 colB = texture(texB, vec2(0.5, 0.5));

    if (uv.x < 0.5) {
        fragColor = colA;  // expected: blue (0,0,1,1)
    } else {
        fragColor = colB;  // expected: green (0,1,0,1) if binding 1 works
    }

    // Border to visualize the split
    if (abs(uv.x - 0.5) < 0.005) {
        fragColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
}
