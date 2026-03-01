#version 450

#include "tonemap.glsl"

layout(set=0, binding=0, std140) uniform World{
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY;

	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY;	
};

layout(set=2, binding=0) uniform samplerCube TEXTURE;
layout(set=5, binding=0) uniform sampler2D NORMAL;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;


void main() {
	vec3 n = normalize(normal);	
	vec3 t = normalize(tangent);
	vec3 bt = normalize(bitangent);
	vec3 nm = texture(NORMAL, texCoord).rgb;
	nm = (nm * 2.0 - 1.0);
	vec3 tangent_normal = mat3(t, bt, n) * nm;
	
	
	vec3 albedo = textureLod(TEXTURE, tangent_normal, 0.0).rgb;
	vec3 tonemapped_color = tonemap(albedo);
	outColor = vec4(tonemapped_color, 1.0);	
	
}