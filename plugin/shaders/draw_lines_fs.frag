#version 440
layout(location = 0) in vec2 vVertex;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vLineOffset;
layout(location = 0) out vec4 fragColor;

void main() {
    float fade = smoothstep(vLineOffset, 1.0, vVertex.y);
    float edgeWidth = fwidth(vVertex.x);
    float xOffset = abs(vVertex.x);
    float smoothEdges = 1.0 - smoothstep(0.5 - edgeWidth, 0.5, xOffset);
    float a = vColor.a * fade * smoothEdges;
    fragColor = vec4(vColor.rgb, a);
}
