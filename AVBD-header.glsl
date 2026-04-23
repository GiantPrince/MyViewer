
#ifndef AVBD_GLSL
#define AVBD_GLSL

layout(push_constant) uniform Push {
    uint rigidbodyCount;
	int color;
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
    vec3 moment;				    	

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


struct Contact
{
    uint feature;
    vec3 rA;
    vec3 rB;
    vec3 C0;
    vec3 penalty;
    vec3 lambda;
    bool stick;
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

layout(set=1, binding=0, std140) buffer UpdatedRigids{
	updatedRigid updatedRigids[];
};

layout(set=2, binding=0, std140) coherent buffer Manifolds{
	Manifold manifolds[];
};

layout(set=3, binding=0, std140) buffer graphColor{
	int colors[];
};

layout(set=4, binding=0) coherent buffer Counter {
	int globalManifoldCount;
};



const float PLANE_EPSILON = 1.0e-5f;
const uint SPHERE = 0;
const uint BOX = 1;
const uint AXIS_FACE_A = 0;
const uint AXIS_FACE_B = 1;
const uint AXIS_EDGE = 2;
const uint AXIS_CLOSEST = 3;
const uint AXIS_SPHERE = 4;
const float FLT_MAX = 3e+38;
const float SAT_AXIS_EPSILON = 1e-6f;
const uint MAX_ITERATIONS = 10;
const float CONTACT_MERGE_DIST_SQ = 0.001f;
const uint MAX_CONTACTS = 8;
const int MAX_POLY_VERTS = 16;
const float COLLISION_MARGIN = 0.01f;
const float PENALTY_MIN = 1.0f;
const float PENALTY_MAX = 10000000000.0f;
const float STICK_THRESH = 0.00001f;

// solver constants

const float dt = 1.0 / 60.0;
const float gravity = -10.0;
const int iterations = 10;

const float betaLin = 10000.0f;
const float betaAng = 100.0f;
    
const float alpha = 0.99f;    
const float gamma = 0.999f;

#endif
