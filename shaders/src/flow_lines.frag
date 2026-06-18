#version 420

layout(binding = 0) uniform sampler2D simTex;
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float GRID_SIZE = 0.117;   // 15px / 128px — matches flux-reference grid_spacing
const float LINE_LENGTH_SCALE = 3.0;
const float LINE_WIDTH = 0.006;   // scaled up from 0.003 to remain visible at larger grid

vec2 decode(vec2 v) { return v * 2.0 - 1.0; }

float distToSegment(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    vec2 closest = a + t * ab;
    return length(p - closest);
}

vec2 sampleVelocitySmoothed(vec2 center, float radius) {
    vec2 sum = vec2(0.0);
    sum += decode(texture(simTex, center).rg) * 0.4;
    sum += decode(texture(simTex, center + vec2(radius, 0.0)).rg) * 0.15;
    sum += decode(texture(simTex, center - vec2(radius, 0.0)).rg) * 0.15;
    sum += decode(texture(simTex, center + vec2(0.0, radius)).rg) * 0.15;
    sum += decode(texture(simTex, center - vec2(0.0, radius)).rg) * 0.15;
    return sum;
}

vec3 velocityColor(vec2 vel, float mag) {
    vec3 dirColor = normalize(vec3(max(vel.x, 0.0), max(vel.y, 0.0), max(-vel.x, 0.0)) + 0.001);
    return mix(vec3(0.1, 0.3, 0.8), dirColor, clamp(mag * 3.0, 0.0, 1.0));
}

void evaluateCell(vec2 uv, vec2 cellCenter, float magThreshold, inout float minDist, inout vec3 bestColor, inout float bestAlpha) {
    if (cellCenter.x < 0.0 || cellCenter.x > 1.0 || cellCenter.y < 0.0 || cellCenter.y > 1.0) return;

    vec2 vel = sampleVelocitySmoothed(cellCenter, GRID_SIZE * 0.3);
    float mag = length(vel);
    if (mag < magThreshold) return;

    vec2 basepoint = cellCenter;
    vec2 endpoint = cellCenter + vel * GRID_SIZE * LINE_LENGTH_SCALE;
    float lineLen = length(endpoint - basepoint);
    if (lineLen < 0.0001) return;

    float dist = distToSegment(uv, basepoint, endpoint);
    if (dist >= minDist) return;

    minDist = dist;

    bestColor = velocityColor(vel, mag);

    float distAlongLine = dot(uv - basepoint, (endpoint - basepoint) / lineLen);
    float lineProgress = clamp(distAlongLine / lineLen, 0.0, 1.0);
    float fadeIn = smoothstep(0.0, 0.3, lineProgress);

    bestAlpha = (1.0 - smoothstep(LINE_WIDTH * 0.5, LINE_WIDTH, dist)) * fadeIn;
    bestAlpha *= smoothstep(0.0, 0.1, mag);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 cellIndexF = uv / GRID_SIZE;
    vec2 cellBase = floor(cellIndexF);

    float minDist = 999.0;
    vec3 bestColor = vec3(0.0);
    float bestAlpha = 0.0;
    float magThreshold = 0.01;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 cellCenter = (cellBase + vec2(float(dx), float(dy)) + 0.5) * GRID_SIZE;
            evaluateCell(uv, cellCenter, magThreshold, minDist, bestColor, bestAlpha);
        }
    }

    vec3 background = vec3(0.02, 0.02, 0.05);
    fragColor = vec4(mix(background, bestColor, bestAlpha), 1.0);
}
