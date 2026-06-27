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

    // Round both ends: circular mask at tip (y=1) and start (y=0)
    float tipDist = length(vec2(vVertex.x, vVertex.y - 1.0));
    float startDist = length(vVertex);
    float roundEdge = fwidth(tipDist);
    float tipRound = 1.0 - smoothstep(0.5 - roundEdge, 0.5, tipDist);
    float startRound = 1.0 - smoothstep(0.5 - roundEdge, 0.5, startDist);
    smoothEdges = min(smoothEdges, min(tipRound, startRound));

    float a = vColor.a * fade * smoothEdges;
    fragColor = vec4(vColor.rgb, a);
}
