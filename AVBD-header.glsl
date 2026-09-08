
#ifndef AVBD_GLSL
#define AVBD_GLSL

layout(push_constant) uniform Push {
    uint rigidbodyCount;
	int color;
 uint hashSize; uint slotsPerBody; float cellSize;
 float dt; float gravity; float betaLin; float alpha; float gamma;
 uint sleepEnabled;
};

struct Rigid
{
	vec3 positionLin;
	float mass;

    vec4 positionAng;

	vec3 initialLin;
	float friction;

    vec4 initialAng;

	vec3 inertialLin;
	float radius;

    vec4 inertialAng;

	vec3 velocityLin;
	uint isStatic;

    vec3 velocityAng;
	uint shape;

	vec3 prevVelocityLin;
	int listHead;

	vec3 size;
    uint quietSteps;
    vec3 moment;
    uint sleeping;

};



struct updatedRigid
{
	vec3 positionLin;
    vec4 positionAng;
};

struct Force {
    uint bodyA;
    uint bodyB;
	int next;
 int nextB;
};


struct FaceFrame {
	int axisIndex;
	vec3 normal;
	vec3 center;
	vec3 u;
	vec3 v;
	float extentU;
	float extentV;
};


// 80 bytes; shared with GpuAVBD.hpp.
struct Contact {
 vec3 rA; uint feature;
 vec3 rB; bool stick;
 vec3 C0; float pad0;
 vec3 penalty; float pad1;
 vec3 lambda; float pad2;
};
struct Manifold {
    Force force;
    Contact contacts[8];
    mat3 basis;
    uint numContacts;
    float friction;    
};

struct OBB {
	vec3 center;
	vec4 rotation;
	vec3 halfExtent;
	vec3 axis[3];
};


struct SatAxis {
	uint type;
	int indexA;
	int indexB;
	float separation;
	vec3 normalAB;
	bool valid;
};

layout(set=0, binding=0, std140) buffer Rigids{
	Rigid rigids[];
};

layout(set=1, binding=0, std430) buffer Scratch { int scratch[]; };
layout(set=2, binding=0, std430) buffer Manifolds { Manifold manifolds[]; };
layout(set=3, binding=0, std430) buffer Graph { int colors[]; };
layout(set=4, binding=0, std430) buffer Counter {
 uint errors; uint staticCount; uint activeCount; uint retainedCount; uint sleepingCount; uint awakeCount;
 uint colorCounts[32];
 uint dispatches[102]; // xyz for 32 body colors, active contacts, awake bodies
};
uint nextOffset() { return hashSize; }
uint staticOffset() { return hashSize + rigidbodyCount; }
uint activeOffset() { return hashSize + 2 * rigidbodyCount; }
uint candidateOffset() { return activeOffset() + rigidbodyCount * slotsPerBody; }
uint awakeOffset() { return candidateOffset() + rigidbodyCount * 65; }
uint hashCell(ivec3 c) {
 uvec3 v = uvec3(c);
 return ((v.x * 73856093u) ^ (v.y * 19349663u) ^ (v.z * 83492791u)) & (hashSize - 1);
}
uint priority(uint x) {
 x = ((x >> 16) ^ x) * 0x45d9f3bu;
 x = ((x >> 16) ^ x) * 0x45d9f3bu;
 return (x >> 16) ^ x;
}
int nextContact(int index, uint body) {
 return manifolds[index].force.bodyA == body ? manifolds[index].force.next : manifolds[index].force.nextB;
}
const float PLANE_EPSILON = 1.0e-5f;
const uint SPHERE = 1;
const uint BOX = 0;
const uint AXIS_FACE_A = 0;
const uint AXIS_FACE_B = 1;
const uint AXIS_EDGE = 2;
const uint AXIS_CLOSEST = 3;
const uint AXIS_SPHERE = 4;
const float FLT_MAX = 3e+38;
const float SAT_AXIS_EPSILON = 1e-6f;
const uint MAX_ITERATIONS = 10;
const float CONTACT_MERGE_DIST_SQ = 0.000001f;
const uint MAX_CONTACTS = 8;
// Clipping a quadrilateral against four side planes adds at most four vertices.
const int MAX_POLY_VERTS = 8;
const float COLLISION_MARGIN = 0.01f;
const float PENALTY_MIN = 1.0f;
const float PENALTY_MAX = 10000000000.0f;
const float STICK_THRESH = 0.00001f;

#endif
