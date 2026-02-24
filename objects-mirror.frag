#version 450

#include "tonemap.glsl"

layout(set=0, binding=0, std140) uniform World{
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY;

	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY;
};

layout(set=2, binding=0) uniform samplerCube TEXTURE;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 view;

layout(location = 0) out vec4 outColor;


void main() {
	vec3 n = normalize(normal);
	vec3 v = normalize(view);
	vec3 albedo = texture(TEXTURE, reflect(v, n)).rgb;	
	vec3 tonemapped_color = tonemap(albedo);
	outColor = vec4(tonemapped_color, 1.0);		
}