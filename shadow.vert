#version 450

struct Transform {
	mat4 CLIP_FROM_MODEL;
	mat4 WORLD_FROM_LOCAL;
	mat4 WORLD_FROM_LOCAL_NORMAL;
};

layout(push_constant) uniform PushConstants {
	uint light_index;
};

layout(set=0, binding=0, std140) readonly buffer Transforms {
	Transform TRANSFORMS[];
};

struct Light{
	mat4 CLIP_FROM_WORLD;
	vec3 direction;
	float angle;

	vec3 position;
	float radius;
	
	vec3 strength;
    float limit;
		
	float fov;
	float blend;
	uint type;
	uint shadowMapIndex;
	
};

layout(set=1, binding=0, std140) readonly buffer Lights{
	Light lights[];
};


layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec4 Tangent;
layout(location = 3) in vec2 TexCoord;

void main()
{
	gl_Position = lights[light_index].CLIP_FROM_WORLD * TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL * vec4(Position, 1.0);
}