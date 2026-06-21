#version 440

layout(binding = 0, std140) uniform FluidUniforms {
    float timestep;
    float dissipation;
    float alpha;
    float r_beta;
    float center_factor;
    float stencil_factor;
} u;

layout(binding = 1) uniform sampler2D velocityTex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    vec2 velocity = texelFetch(velocityTex, pos, 0).xy;
    vec2 l = texelFetch(velocityTex, pos + ivec2(-1, 0), 0).xy;
    vec2 r = texelFetch(velocityTex, pos + ivec2(1, 0), 0).xy;
    vec2 b = texelFetch(velocityTex, pos + ivec2(0, -1), 0).xy;
    vec2 t = texelFetch(velocityTex, pos + ivec2(0, 1), 0).xy;
    vec2 newVel = u.stencil_factor * (l + r + b + t + u.center_factor * velocity);
    fragColor = vec4(newVel, 0.0, 1.0);
}
