#version 440

layout(binding = 0, std140) uniform FluidUniforms {
    float timestep;
    float dissipation;
    float alpha;
    float r_beta;
    float center_factor;
    float stencil_factor;
} u;

layout(binding = 1, std140) uniform DirectionBlock {
    float direction;
} d;

layout(binding = 2) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(velocityTex, 0);

    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 samplePos = vec2(pos) + 0.5;
    vec2 advectedPos = (samplePos - d.direction * u.timestep * velocity) / vec2(size);
    float decay = 1.0 + u.dissipation * u.timestep;
    vec2 newVel = textureLod(velocityTex, advectedPos, 0.0).xy / decay;

    fragColor = vec4(newVel, 0.0, 1.0);
}
