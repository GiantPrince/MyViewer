#version 450

layout(set=0, binding=0, std140) uniform World{
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY;

	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY;
};

layout(set=2, binding=0) uniform sampler2D TEXTURE;
layout(set=5, binding=0) uniform sampler2D NORMAL;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 2) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

layout(location = 0) out vec4 outColor;

void main() {
	vec3 n = normalize(normal);		
	vec3 t = normalize(tangent);
	vec3 bt = normalize(bitangent);
	vec3 nm = texture(NORMAL, texCoord).rgb;
	nm = (nm * 2.0 - 1.0);
	vec3 tangent_normal = mat3(t, bt, n) * nm;
	
	vec3 albedo = texture(TEXTURE, texCoord).rgb / 3.1415926;

	vec3 e = SKY_ENERGY * vec3(0.5 * dot(tangent_normal, SKY_DIRECTION) + 0.5)
		+ SUN_ENERGY * vec3(max(0.0, dot(tangent_normal, SUN_DIRECTION)));
	outColor = vec4(albedo * e, 1.0);
}