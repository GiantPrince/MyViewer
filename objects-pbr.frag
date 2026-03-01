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
layout(set=5, binding=0) uniform sampler2D NORMAL;
layout(set=6, binding=0) uniform sampler2D ROUGHNESS;
layout(set=7, binding=0) uniform sampler2D METALNESS;
layout(set=8, binding=0) uniform samplerCube PRE_FILTERED_ENV;
layout(set=9, binding=0) uniform sampler2D BRDF;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;
layout(location = 3) in vec3 view;

layout(location = 0) out vec4 outColor;

//https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
vec3 ApproximateSpecularIBL(vec3 SpecularColor , float Roughness, vec3 N, vec3 V, int MAX_MIPMAP_LEVEL)
{
	float NoV = clamp(dot(N, V), 0, 1);
	vec3 R = reflect(V, N);
	vec3 PrefilteredColor = textureLod(PRE_FILTERED_ENV, R, Roughness * MAX_MIPMAP_LEVEL).rgb;
	vec2 EnvBRDF = texture(BRDF, vec2(1.0 - Roughness, NoV)).rg;
	return PrefilteredColor * ( SpecularColor * EnvBRDF.x + EnvBRDF.y );
}

void main() {
	vec3 n = normalize(normal);
	vec3 t = normalize(tangent);
	vec3 bt = normalize(bitangent);
	vec3 nm = texture(NORMAL, texCoord).rgb;
	nm = (nm * 2.0 - 1.0);
	vec3 tangent_normal = normalize(mat3(t, bt, n) * nm);
	
	//vec3 radiance = texture(ENV, tangent_normal).rgb;
	
	vec3 albedo = texture(TEXTURE, texCoord).rgb;
	float roughness = texture(ROUGHNESS, texCoord).r;
	float metalness = texture(METALNESS, texCoord).r;
		
	vec3 tint = mix(vec3(0.04), albedo, metalness);
	int levels = textureQueryLevels(PRE_FILTERED_ENV);
	vec3 spec = ApproximateSpecularIBL(tint, roughness, tangent_normal, view, levels - 1);
	vec3 diffAlbedo = mix(albedo, vec3(0.0), metalness);

	vec3 e = texture(ENV, tangent_normal).rgb;
	vec3 radiance = spec + diffAlbedo * e;	
	vec3 tonemapped_color = tonemap(radiance);
		
	outColor = vec4(tonemapped_color, 1.0);
}