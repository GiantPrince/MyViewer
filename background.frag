#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
    float time;
};

void main() {
    outColor = vec4(0.5 + 0.5 * sin(time * 0.5 + position.xyx + vec3(0, 1.33, 2.67)), 1.0);
}
