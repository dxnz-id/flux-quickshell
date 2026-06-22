#version 440
layout(binding = 0) uniform sampler2D velocityTex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 vel = texture(velocityTex, vTexCoord).xy;
    float speed = length(vel);
    vec3 c = mix(vec3(0.0, 0.0, 0.08), vec3(1.0, 0.95, 0.8), clamp(speed * 5.0, 0.0, 1.0));
    vec3 color = mix(c, vec3(1.0, 0.2, 0.05), clamp(speed * 5.0 - 1.0, 0.0, 1.0));
    fragColor = vec4(color, 1.0);
}
