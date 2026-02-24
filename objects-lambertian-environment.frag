#version 450

#include "tonemap.glsl"

layout(set=0, binding=0, std140) uniform World{
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY;

	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY;
};


layout(set=2, binding=0) uniform sampler2D TEXTURE;
layout(set=4, binding=0) uniform samplerCube ENV;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 n = normalize(normal);
	
	vec3 albedo = texture(TEXTURE, texCoord).rgb;
	
	vec3 e = texture(ENV, n).rgb;
	vec3 radiance = albedo * e;
	
	vec3 tonemapped_color = tonemap(radiance);
		
	outColor = vec4(tonemapped_color, 1.0);
}