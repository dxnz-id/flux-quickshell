#version 440

layout(binding = 0) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    float l = texelFetch(velocityTex, pos + ivec2(-1, 0), 0).x;
    float r = texelFetch(velocityTex, pos + ivec2(1, 0), 0).x;
    float t = texelFetch(velocityTex, pos + ivec2(0, 1), 0).y;
    float b = texelFetch(velocityTex, pos + ivec2(0, -1), 0).y;
    float newDiv = 0.5 * ((r - l) + (t - b));
    fragColor = vec4(newDiv, 0.0, 0.0, 1.0);
}
