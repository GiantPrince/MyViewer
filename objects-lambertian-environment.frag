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


layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;


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
	return pow(clamp(1.0 - pow(distance / radius, 4.0), 0.0, 1.0), 2.0) / (pow(distance, 2.0) + 1);
}

void main() {
	vec3 n = normalize(normal);
	vec3 t = normalize(tangent);
	vec3 bt = normalize(bitangent);
	vec3 nm = texture(NORMAL, texCoord).rgb;
	nm = (nm * 2.0 - 1.0);
	vec3 tangent_normal = normalize(mat3(t, bt, n) * nm);
	
	vec3 albedo = texture(TEXTURE, texCoord).rgb;
		
	vec3 e = vec3(0.0);//textureLod(ENV, tangent_normal, 0.0).rgb;

	for (int i = 0; i < light_count; i++) {
		if (lights[i].type == LIGHT_TYPE_SUN) {
			// dir normalized
			float nl = dot(tangent_normal, lights[i].direction);
			float sinTheta = sin(lights[i].angle / 2.0);

			e += lights[i].strength * computeHorizon(nl, sinTheta);
		}
		else if (lights[i].type == LIGHT_TYPE_SPHERE) {
			vec3 lightDir = lights[i].position - position;
			float distance = length(lightDir);
			lightDir = normalize(lightDir);

			float nl = dot(tangent_normal, lightDir);
			float sinTheta = lights[i].radius / distance;

			e += lights[i].strength * computeHorizon(nl, sinTheta) * falloff(distance, lights[i].limit);
		}
		else if (lights[i].type == LIGHT_TYPE_SPOT) {
			vec3 lightDir = lights[i].position - position;
			float distance = length(lightDir);
			lightDir = normalize(lightDir);

			float cosTheta = dot(-lightDir, lights[i].direction);
			float angle = acos(cosTheta);

			float outerAngle = lights[i].fov / 2.0;
			float innerAngle = outerAngle * (1.0 - lights[i].blend);

			float spotEffect = smoothstep(outerAngle, innerAngle, angle);

			float nl = dot(tangent_normal, lightDir);
			float sinTheta = lights[i].radius / distance;

			e += lights[i].strength * computeHorizon(nl, sinTheta) * falloff(distance, lights[i].limit) * spotEffect;
		}
	}

	vec3 radiance = albedo * e;
	
	vec3 tonemapped_color = tonemap(radiance);
		
	outColor = vec4(tonemapped_color, 1.0);
}