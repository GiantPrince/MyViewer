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

struct Light{
	vec3 direction;
	float angle;

	vec3 position;
	float radius;
	
	vec3 strength;
    float limit;
		
	float fov;
	float blend;
	uint type;
	
};

layout(set=10, binding=0, std140) readonly buffer Lights{
	Light lights[];
};

const int LIGHT_TYPE_SUN = 0;
const int LIGHT_TYPE_SPHERE = 1;
const int LIGHT_TYPE_SPOT = 2;
const float PI = 3.14159265359;


layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;
layout(location = 3) in vec3 view;

layout(location = 0) out vec4 outColor;

float smoothHorizon(float nl, float sinTheta) {
	return nl * nl / (4.0 * sinTheta) + nl / 2.0 + sinTheta / 4.0;
}

// here sinTheta is the sine of half the angle of the disc
float computeHorizon(float nl, float sinTheta) {
	if (nl > sinTheta) {
		return nl;
	}
	else if (nl < -sinTheta) {
		return 0;
	}
	else {
		// smooth function
		return smoothHorizon(nl, sinTheta);
	}
}

float falloff(float distance, float radius) {	
	return pow(max(0.0, 1.0 - pow(distance / radius, 4.0)), 2.0) / (pow(distance, 2.0) + 1);
}

//https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
vec3 ApproximateSpecularIBL(vec3 SpecularColor , float Roughness, vec3 N, vec3 V, int MAX_MIPMAP_LEVEL)
{
	float NoV = clamp(dot(N, V), 0, 1);
	vec3 R = reflect(V, N);
	vec3 PrefilteredColor = textureLod(PRE_FILTERED_ENV, R, Roughness * MAX_MIPMAP_LEVEL).rgb;
	vec2 EnvBRDF = texture(BRDF, vec2(Roughness, NoV)).rg;
	return PrefilteredColor * ( SpecularColor * EnvBRDF.x + EnvBRDF.y );
}

vec3 representativeDirection(vec3 shadePosition, vec3 normal, vec3 viewDirection, Light light) {
	
	if (light.type == LIGHT_TYPE_SUN) {
		vec3 l = -normalize(light.direction);
		vec3 r = normalize(reflect(-viewDirection, normal));
		float lDotR = dot(l, r);
		float cosTheta = cos(light.angle / 2);
		if (lDotR >= cosTheta) {
			return l;
		}
		else {
			return l * cosTheta + (r - lDotR * l) * sin(light.angle / 2);
		}		
	}
	else if (light.type == LIGHT_TYPE_SPHERE) {
		vec3 L = light.position - shadePosition;
		vec3 r = reflect(-normalize(viewDirection), normal);
		vec3 centerToRay = dot(L, r) * r - L;
		vec3 closestPoint = L + centerToRay * clamp(light.radius / length(centerToRay), 0.0, 1.0);
		return normalize(closestPoint);
	}
	else if (light.type == LIGHT_TYPE_SPOT) {
		vec3 L = light.position - shadePosition;
		vec3 r = reflect(normalize(-viewDirection), normal);
		vec3 centerToRay = dot(L, r) * r - L;
		vec3 closestPoint = L + centerToRay * clamp(light.radius / length(centerToRay), 0.0, 1.0);
		return normalize(closestPoint);
	}
	else {
		return vec3(0.0);
	}
}

float D_GGX(float NdotH, float alpha) {
	
	float a2 = alpha * alpha;
	return a2 / (PI * (NdotH * NdotH * (a2 - 1.0) + 1.0) * (NdotH * NdotH * (a2 - 1.0) + 1.0));
}

float G_helper(float NdotX, float k) {
	return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float roughness, float NdotV, float NdotL) {
	float k = roughness*roughness / 2.0;
	return G_helper(NdotL, k) * G_helper(NdotV, k);
}

vec3 F_Schlick(float VdotH, vec3 F0)
{
	float Fc = pow(1.0 - VdotH, 5.0);
	return (1.0 - Fc) * F0 + Fc;    
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

	float alpha = roughness * roughness;

	for (int i = 0; i < light_count; i++) {
		// find representative direction
		vec3 L = representativeDirection(position, tangent_normal, view, lights[i]);

		float alpha_prime = 0.0f;

		if (lights[i].type == LIGHT_TYPE_SUN) {
			// dir normalized
			float nl = dot(tangent_normal, -lights[i].direction);
			float sinTheta = sin(lights[i].angle / 2.0);

			e += lights[i].strength * computeHorizon(nl, sinTheta);

			alpha_prime = clamp(alpha + lights[i].angle / 2.0, 0.0, 1.0);
		}
		else if (lights[i].type == LIGHT_TYPE_SPHERE) {
			vec3 lightDir = lights[i].position - position;
			float distance = length(lightDir);
			lightDir = normalize(lightDir);

			float nl = dot(tangent_normal, lightDir);
			float sinTheta = lights[i].radius / distance;

			e += lights[i].strength * computeHorizon(nl, sinTheta) * falloff(distance, lights[i].limit);

			alpha_prime = clamp(alpha + lights[i].radius / distance / 2.0, 0.0, 1.0);
		}
		else if (lights[i].type == LIGHT_TYPE_SPOT) {
			vec3 lightDir = lights[i].position - position;
			float distance = length(lightDir);
			lightDir = normalize(lightDir);

			float cosTheta = dot(-lightDir, lights[i].direction);
			float angle = acos(cosTheta);

			float outerAngle = lights[i].fov / 2.0;
			float innerAngle = outerAngle * (1.0 - lights[i].blend);

			float spotEffect = 0;
			if (angle < innerAngle) {
				spotEffect = 1.0;
			}
			else if (angle > outerAngle) {
				spotEffect = 0.0;
			}
			else {
				spotEffect = (outerAngle - angle) / (outerAngle - innerAngle);
			}		

			float nl = dot(tangent_normal, lightDir);
			float sinTheta = lights[i].radius / distance;

			e += lights[i].strength * computeHorizon(nl, sinTheta) * falloff(distance, lights[i].limit) * spotEffect;

			alpha_prime = clamp(alpha + lights[i].radius / distance / 2.0, 0.0, 1.0);
		}

		vec3 N = tangent_normal;
		vec3 V = normalize(view);		

		// be careful when calculating the half vector, both should be normalized
		vec3 H = normalize(L + V);
		
		

		float NdotL = clamp(dot(N, L), 0.0, 1.0);
		float NdotV = clamp(dot(N, V), 0.0, 1.0);
		float NdotH = clamp(dot(N, H), 0.0, 1.0);
		float VdotH = clamp(dot(V, H), 0.0, 1.0);

		float finalSpecEffect = 1.0;
		if (lights[i].type == LIGHT_TYPE_SPOT) {
			vec3 l = normalize(position - lights[i].position);
			float cosL = dot(l, normalize(lights[i].direction));
			float L_angle = acos(clamp(cosL, -1.0, 1.0));
        	float outerAngle = lights[i].fov / 2.0;
			float innerAngle = outerAngle * (1.0 - lights[i].blend);

			finalSpecEffect = clamp((outerAngle - L_angle) / (outerAngle - innerAngle), 0.0, 1.0);
        			
			float distance = length(lights[i].position - position);
			finalSpecEffect *= falloff(distance, lights[i].limit);
		} 
		else if (lights[i].type == LIGHT_TYPE_SPHERE) {
			float distance = length(lights[i].position - position);
			finalSpecEffect = falloff(distance, lights[i].limit);
		}

		
		
		if (NdotL > 0.0 && NdotV > 0.0 && NdotH > 0.0 && VdotH > 0.0) {				
		float D = D_GGX(NdotH, alpha_prime);
		float G = G_Smith(NdotV, NdotL, roughness);
		vec3  F = F_Schlick(VdotH, tint);
		vec3 lightSpec = (D * G * F) / (4 * NdotV * NdotL) * (alpha / alpha_prime) * (alpha / alpha_prime);

		
		radiance += lightSpec * lights[i].strength * NdotL * finalSpecEffect;
		}

		
	}
	radiance += diffAlbedo * e / 3.1415926;
	//radiance = albedo;

	vec3 tonemapped_color = tonemap(radiance);
		
	outColor = vec4(tonemapped_color, 1.0);
}