#version 440
layout(location = 0) in vec2 aVertex;
layout(location = 1) in vec2 aBasepoint;

layout(binding = 0) uniform sampler2D uVelTex;
layout(binding = 1) uniform sampler2D uStateTex;
layout(binding = 2, std140) uniform LineUniforms {
    float uAspect;
    float uZoom;
    float uLineWidth;
    float uLineLength;
    float uLineBeginOffset;
    float uLineVariance;
    float uDeltaTime;
    float uGridCols;
    float uGridSpacing;
    float _pad0;
    float _pad1;
    float _pad2;
};

layout(location = 0) out vec2 vVertex;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vLineOffset;

void main() {
    int idx = gl_InstanceIndex;
    int base = idx * 3;
    ivec2 p0 = ivec2(base + 0, 0);

    vec4 t0 = texelFetch(uStateTex, p0, 0);
    vec2 endpoint = t0.xy;

    vec4 color = texelFetch(uStateTex, ivec2(base + 1, 0), 0);
    float width = texelFetch(uStateTex, ivec2(base + 2, 0), 0).w;

    vec2 xBasis = vec2(-endpoint.y, endpoint.x);
    xBasis /= max(length(xBasis), 1e-10);

    vec2 point = vec2(uAspect, 1.0) * uZoom * (aBasepoint * 2.0 - 1.0)
        + endpoint * aVertex.y
        + uLineWidth * width * xBasis * aVertex.x;
    point.x /= uAspect;

    float shortLineBoost = 1.0 + (uLineWidth * width) / max(length(endpoint), 1e-10);
    vLineOffset = uLineBeginOffset / shortLineBoost;
    vVertex = aVertex;
    vColor = color;

    gl_Position = vec4(point, 0.0, 1.0);
}
