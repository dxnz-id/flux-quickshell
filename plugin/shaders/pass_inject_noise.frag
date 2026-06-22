#version 440

layout(binding = 0) uniform sampler2D noiseTex;
layout(binding = 1) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 velSize = textureSize(velocityTex, 0);
    vec2 uv = (gl_FragCoord.xy + 0.5) / vec2(velSize);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 noise = texture(noiseTex, uv).xy;

    vec2 newVel = velocity + (1.0 / 60.0) * noise;
    fragColor = vec4(newVel, 0.0, 0.0);
}
