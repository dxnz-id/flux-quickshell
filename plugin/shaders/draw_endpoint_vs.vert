#version 440
layout(location = 0) in vec2 aVertex;
layout(location = 1) in vec2 aBasepoint;

layout(binding = 0, std140) uniform LineUniforms {
    float uAspect;
    float uZoom;
    float uLineWidth;
    float uLineLength;
    float uLineBeginOffset;
    float uLineVariance;
    float uDeltaTime;
    float uGridCols;
    float uGridRows;
    float uGridSpacingX;
    float uGridSpacingY;
    float uNoiseScaleX;
    float uNoiseScaleY;
    float uNoiseOffset1;
    float uNoiseOffset2;
    float uNoiseBlendFactor;
    float uColorMode;
    float _pad0[3];
};
layout(binding = 1) uniform sampler2D uStateTex;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vVertex;
layout(location = 2) out vec2 vMidpointVector;

void main() {
    int texW = 256;
    int base = int(gl_InstanceIndex) * 3;
    ivec2 p0 = ivec2(base % texW, base / texW);

    vec4 t0 = texelFetch(uStateTex, p0, 0);
    vec2 endpoint = t0.xy;

    vec4 color = texelFetch(uStateTex, ivec2((base + 1) % texW, (base + 1) / texW), 0);
    float width = texelFetch(uStateTex, ivec2((base + 2) % texW, (base + 2) / texW), 0).w;

    vec2 point = vec2(uAspect, 1.0) * uZoom * (aBasepoint * 2.0 - 1.0)
        + endpoint
        + uLineWidth * width * aVertex;
    point.x /= uAspect;

    gl_Position = vec4(point, 0.0, 1.0);
    vColor = color;
    vVertex = aVertex;
    vMidpointVector = vec2(endpoint.y, -endpoint.x);
}
