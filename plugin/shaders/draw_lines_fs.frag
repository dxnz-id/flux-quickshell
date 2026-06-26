#version 440
layout(location = 0) in vec2 vVertex;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vLineOffset;
layout(location = 0) out vec4 fragColor;

void main() {
    float a = vColor.a;
    float lineDist = abs(vVertex.x);
    float alongLine = vVertex.y - vLineOffset;
    float d = max(lineDist, vLineOffset - alongLine);
    a *= 1.0 - smoothstep(0.0, 1.5, d);
    fragColor = vec4(vColor.rgb, a);
}