#version 440
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vVertex;
layout(location = 2) in vec2 vMidpointVector;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 color = vColor.rgb;
    float endpointOpacity = clamp(vColor.a + max(0.0, 1.0 - vColor.a), 0.0, 1.0);
    vec4 topColor = vec4(color, endpointOpacity);
    vec3 premultiplied = color * vColor.a;
    vec4 bottomColor = vec4(color * endpointOpacity - premultiplied, 1.0);

    float side = (vVertex.x - vMidpointVector.x) * (-vMidpointVector.y)
        - (vVertex.y - vMidpointVector.y) * (-vMidpointVector.x);
    vec4 outColor = (side > 0.0) ? topColor : bottomColor;

    float dist = length(vVertex);
    float smoothEdges = 1.0 - smoothstep(1.0 - fwidth(dist), 1.0, dist);
    outColor.a *= smoothEdges;

    fragColor = outColor;
}
