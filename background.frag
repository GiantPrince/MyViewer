#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
    float time;
};

void main() {
    vec2 p = position - vec2(0.5);     

    float r = length(p);
    float theta = atan(p.y,p.x);

    float wave = 0.33 + 0.33 * sin(10.0 * r + time * 2.0 + 5.0 * theta);   
    float y = wave + 0.33 + 0.33 * sin(time);
    float z = wave + 0.67 + 0.67 * cos(time);
    outColor = vec4(wave, y, z, 1.0);
}
