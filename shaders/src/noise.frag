#version 420

layout(binding = 0) uniform sampler2D simTex;

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

const float RES = 128.0;
const float TIMESTEP = 1.0;
const float TIME = 1.0;
const float GLOBAL_MULT = 0.45;

const vec2 SCALE0 = vec2(2.8, 2.8);
const vec2 SCALE1 = vec2(15.0, 15.0);
const vec2 SCALE2 = vec2(30.0, 30.0);
const float MULT0 = 1.0;
const float MULT1 = 0.7;
const float MULT2 = 0.5;
const float SPEED0 = 0.001;
const float SPEED1 = 0.006;
const float SPEED2 = 0.012;

vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    i = mod289(i);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
      + i.y + vec4(0.0, i1.y, i2.y, 1.0))
      + i.x + vec4(0.0, i1.x, i2.x, 1.0));
    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

float channelNoise(vec2 uv, float scale, float speed, float zOffset) {
    return snoise(vec3(uv * scale, TIME * speed + zOffset));
}

void main() {
    vec2 uv = qt_TexCoord0;

    vec4 tc = texture(simTex, uv);
    vec2 v = tc.rg * 2.0 - 1.0;

    vec2 n = vec2(0.0);
    float zOff = 50.0;
    n.x += MULT0 * channelNoise(uv, SCALE0.x, SPEED0, 0.0);
    n.y += MULT0 * channelNoise(uv, SCALE0.y, SPEED0, zOff);
    n.x += MULT1 * channelNoise(uv, SCALE1.x, SPEED1, 0.0);
    n.y += MULT1 * channelNoise(uv, SCALE1.y, SPEED1, zOff);
    n.x += MULT2 * channelNoise(uv, SCALE2.x, SPEED2, 0.0);
    n.y += MULT2 * channelNoise(uv, SCALE2.y, SPEED2, zOff);

    n *= GLOBAL_MULT;

    vec2 newVel = v + TIMESTEP * n;

    fragColor = vec4(newVel * 0.5 + 0.5, tc.b, 1.0);
}
