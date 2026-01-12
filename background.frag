#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
	float time;
};

void main() {
	outColor = vec4(fract(position.x + time), position.y, 0.0, 1.0);
}