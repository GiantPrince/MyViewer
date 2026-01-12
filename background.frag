#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
	float time;
};

bool heart(vec2 p) {
	float x = (p.x - 0.5) / 0.1;
	float y = -(p.y - 0.5) / 0.1;
	//return abs(x) <= 0.2;
	return (pow(x * x + y * y - 1, 3.0) - x * x * y * y * y) <= 0;
}

void main() {
    if (heart(position)) {
		outColor = vec4(0.5 + 0.5 * cos(position.x * time), 0.5 + 0.5 * sin(position.y * time), 0.2, 1.0);
	}
	else {
		outColor = vec4(1.0, 1.0, 1.0, 1.0);
	}
}