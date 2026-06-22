#version 440

layout(binding = 0, std140) uniform NoiseParams {
    vec4 elapsed;
    vec4 ch0;
    vec4 ch1;
    vec4 ch2;
};

layout(location = 0) out vec4 fragColor;

const float NOISE_MULT = 0.45;
const float NS = 256.0;

struct Chan { float scale, inc, mult; };
const Chan CH[3] = Chan[](
    Chan(2.8,  0.001, 1.0),
    Chan(15.0, 0.006, 0.7),
    Chan(30.0, 0.012, 0.5)
);

float mod289(float x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g, l.zxy);
    vec3 i2 = max(g, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - 0.5;
    i = mod289(vec4(i.x, i.y, i.z, 0.0)).xyz;
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0)) +
        i.y + vec4(0.0, i1.y, i2.y, 1.0)) +
        i.x + vec4(0.0, i1.x, i2.x, 1.0));
    vec4 j = p - 49.0 * floor(p * (1.0 / 49.0));
    vec4 x_ = floor(j * (1.0 / 7.0));
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * (2.0 / 7.0) + 0.5 / 7.0 - 1.0;
    vec4 y = y_ * (2.0 / 7.0) + 0.5 / 7.0 - 1.0;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 g0 = vec3(a0.xy, h.x);
    vec3 g1 = vec3(a0.zw, h.y);
    vec3 g2 = vec3(a1.xy, h.z);
    vec3 g3 = vec3(a1.zw, h.w);
    vec4 norm = inversesqrt(vec4(dot(g0, g0), dot(g1, g1), dot(g2, g2), dot(g3, g3)));
    g0 *= norm.x; g1 *= norm.y; g2 *= norm.z; g3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4(0.0));
    m = m * m;
    m = m * m;
    vec4 px = vec4(dot(x0, g0), dot(x1, g1), dot(x2, g2), dot(x3, g3));
    return 42.0 * dot(m, px);
}

vec2 makeNoisePair(vec3 params) {
    return vec2(snoise(params), snoise(params + vec3(8.0, -8.0, 0.0)));
}

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    vec2 texelPos = (vec2(pos) + 0.5) / NS;

    vec4 chData[3] = vec4[](ch0, ch1, ch2);
    vec2 noise = vec2(0.0);
    for (int i = 0; i < 3; i++) {
        float scaleOsc = 1.0 + 0.15 * sin(0.01 * elapsed.x * 6.2831853);
        float s = CH[i].scale * scaleOsc;
        vec2 sc = s * texelPos;
        vec2 n1 = makeNoisePair(vec3(sc, chData[i].x));
        vec2 n = n1;
        if (chData[i].z > 0.0) {
            vec2 n2 = makeNoisePair(vec3(sc, chData[i].y));
            n = mix(n1, n2, chData[i].z);
        }
        noise += CH[i].mult * n;
    }

    fragColor = vec4(noise * NOISE_MULT, 0.0, 0.0);
}
