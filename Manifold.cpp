//https://github.com/savant117/avbd-demo3d/blob/main/source/collide.cpp


#include "solver.hpp"

#include <stdexcept>

Manifold::Manifold(Solver* solver, Rigid* bodyA, Rigid* bodyB)
	: Force(solver, bodyA, bodyB), numContacts(0)
{
}

bool Manifold::initialize()
{
	friction = sqrtf(bodyA->friction * bodyB->friction);

	Contact newContacts[8] = { 0 };
	int newNumContacts = collide(bodyA, bodyB, newContacts, basis);

	for (int i = 0; i < newNumContacts; i++)
	{
		for (int j = 0; j < numContacts; j++)
		{
			if (newContacts[i].feature.key == contacts[j].feature.key)
			{
				float3 newRA = newContacts[i].rA;
				float3 newRB = newContacts[i].rB;
				newContacts[i] = contacts[j];

				if (!contacts[j].stick)
				{
					newContacts[i].rA = newRA;
					newContacts[i].rB = newRB;
				}
				break;
			}
		}
	}

	numContacts = newNumContacts;
	for (int i = 0; i < numContacts; i++)
		contacts[i] = newContacts[i];

	for (int i = 0; i < numContacts; i++) {
		float3 xA = transform(bodyA->positionLin, bodyA->positionAng, contacts[i].rA);
		float3 xB = transform(bodyB->positionLin, bodyB->positionAng, contacts[i].rB);
		contacts[i].C0 = basis * (xA - xB) + float3{ COLLISION_MARGIN, 0, 0 };

		contacts[i].lambda = contacts[i].lambda * solver->alpha * solver->gamma;
		contacts[i].penalty = clamp(contacts[i].penalty * solver->gamma, PENALTY_MIN, PENALTY_MAX);
	}

	return numContacts > 0;
}

void Manifold::updatePrimal(Rigid* body, float alpha, float3x3& lhsLin, float3x3& lhsAng, float3x3& lhsCross, float3& rhsLin, float3& rhsAng)
{
	float3 dqALin = bodyA->positionLin - bodyA->initialLin;
	float3 dqAAng = bodyA->positionAng - bodyA->initialAng;
	float3 dqBLin = bodyB->positionLin - bodyB->initialLin;
	float3 dqBAng = bodyB->positionAng - bodyB->initialAng;

	for (int i = 0; i < numContacts; i++)
	{
		float3 rAWorld = rotate(bodyA->positionAng, contacts[i].rA);
		float3 rBWorld = rotate(bodyB->positionAng, contacts[i].rB);

		float3x3 jALin = basis;
		float3x3 jBLin = -basis;
		float3x3 jAAng = float3x3{ cross(rAWorld, jALin[0]), cross(rAWorld, jALin[1]), cross(rAWorld, jALin[2]) };
		float3x3 jBAng = float3x3{ cross(rBWorld, jBLin[0]), cross(rBWorld, jBLin[1]), cross(rBWorld, jBLin[2]) };

		float3x3 K = diagonal(contacts[i].penalty.x, contacts[i].penalty.y, contacts[i].penalty.z);
		float3 C = contacts[i].C0 * (1 - alpha) + jALin * dqALin + jBLin * dqBLin + jAAng * dqAAng + jBAng * dqBAng;

		float3 F = K * C + contacts[i].lambda;

		F[0] = min(F[0], 0.0f);

		float bounds = fabsf(F[0]) * friction;
		float frictionScale = length(float2{ F[1], F[2] });
		if (frictionScale > bounds && frictionScale > 0)
		{
			F[1] *= bounds / frictionScale;
			F[2] *= bounds / frictionScale;
		}

		float3x3 jLin = body == bodyA ? jALin : jBLin;
		float3x3 jAng = body == bodyA ? jAAng : jBAng;

		float3x3 jLinT = transpose(jLin);
		float3x3 jAngT = transpose(jAng);
		float3x3 jAngTk = jAngT * K;

		lhsLin += jLinT * K * jLin;
		lhsAng += jAngTk * jAng;
		lhsCross += jAngTk * jLin;

		rhsLin += jLinT * F;
		rhsAng += jAngT * F;
	}
}

void Manifold::updateDual(float alpha)
{
	float3 dqALin = bodyA->positionLin - bodyA->initialLin;
	float3 dqAAng = bodyA->positionAng - bodyA->initialAng;
	float3 dqBLin = bodyB->positionLin - bodyB->initialLin;
	float3 dqBAng = bodyB->positionAng - bodyB->initialAng;

	for (int i = 0; i < numContacts; i++) {
		float3 rAWorld = rotate(bodyA->positionAng, contacts[i].rA);
		float3 rBWorld = rotate(bodyB->positionAng, contacts[i].rB);

		float3x3 jALin = basis;
		float3x3 jBLin = -basis;
		float3x3 jAAng = float3x3{ cross(rAWorld, jALin[0]), cross(rAWorld, jALin[1]), cross(rAWorld, jALin[2]) };
		float3x3 jBAng = float3x3{ cross(rBWorld, jBLin[0]), cross(rBWorld, jBLin[1]), cross(rBWorld, jBLin[2]) };

		float3x3 K = diagonal(contacts[i].penalty.x, contacts[i].penalty.y, contacts[i].penalty.z);
		float3 C = contacts[i].C0 * (1 - alpha) + jALin * dqALin + jBLin * dqBLin + jAAng * dqAAng + jBAng * dqBAng;

		float3 F = K * C + contacts[i].lambda;

		F[0] = min(F[0], 0.0f);

		float bounds = fabsf(F[0]) * friction;
		float frictionScale = length(float2{ F[1], F[2] });
		if (frictionScale > bounds && frictionScale > 0) {
			F[1] *= bounds / frictionScale;
			F[2] *= bounds / frictionScale;
		}

		contacts[i].lambda = F;

		if (F[0] < 0) {
			contacts[i].penalty[0] = min(contacts[i].penalty[0] + solver->betaLin * fabsf(C[0]), PENALTY_MAX);
		}
		if (frictionScale <= bounds) {
			contacts[i].penalty[1] = min(contacts[i].penalty[1] + solver->betaLin * fabsf(C[1]), PENALTY_MAX);
			contacts[i].penalty[2] = min(contacts[i].penalty[2] + solver->betaLin * fabsf(C[2]), PENALTY_MAX);
			contacts[i].stick = length(float2{ C[1], C[2] }) < STICK_THRESH;
		}
	}
}


#include <cfloat>
#include <cmath>

namespace
{
	constexpr int MAX_CONTACTS = 8;
	constexpr int MAX_POLY_VERTS = 16;
	constexpr float SAT_AXIS_EPSILON = 1.0e-6f;
	constexpr float PLANE_EPSILON = 1.0e-5f;
	constexpr float CONTACT_MERGE_DIST_SQ = 1.0e-6f;

	enum AxisType {
		AXIS_FACE_A,
		AXIS_FACE_B,
		AXIS_EDGE,
		AXIS_CLOSEST,
		AXIS_SPHERE
	};

	struct OBB {
		float3 center;
		quat rotation;
		float3 half;
		float3 axis[3];
	};

	struct SatAxis {
		AxisType type;
		int indexA;
		int indexB;
		float separation;
		float3 normalAB;
		bool valid;
	};

	struct FaceFrame {
		int axisIndex;
		float3 normal;
		float3 center;
		float3 u;
		float3 v;
		float extentU;
		float extentV;
	};

	inline OBB makeOBB(const Rigid* body) {
		OBB box{};
		box.center = body->positionLin;
		box.rotation = body->positionAng;
		box.half = body->size * 0.5f;
		box.axis[0] = rotate(body->positionAng, float3{ 1.0f, 0.0f, 0.0f });
		box.axis[1] = rotate(body->positionAng, float3{ 0.0f, 1.0f, 0.0f });
		box.axis[2] = rotate(body->positionAng, float3{ 0.0f, 0.0f, 1.0f });
		return box;
	}

	inline float absDot(float3 a, float3 b) {
		return fabsf(dot(a, b));
	}

	inline float3 supportPoint(const OBB& box, const float3& dir) {
		float sx = dot(dir, box.axis[0]) >= 0.0f ? 1.0f : -1.0f;
		float sy = dot(dir, box.axis[1]) >= 0.0f ? 1.0f : -1.0f;
		float sz = dot(dir, box.axis[2]) >= 0.0f ? 1.0f : -1.0f;

		return box.center
			+ box.axis[0] * (box.half.x * sx)
			+ box.axis[1] * (box.half.y * sy)
			+ box.axis[2] * (box.half.z * sz);
	}

	inline void getFaceAxes(const OBB& box, int axisIndex, float3& u, float3& v, float& extentU, float& extentV) {
		int uaxis = (axisIndex + 1) % 3;
		int vaxis = (axisIndex + 2) % 3;
		u = box.axis[uaxis];
		v = box.axis[vaxis];
		extentU = box.half[uaxis];
		extentV = box.half[vaxis];

	}

	inline void buildFaceFrame(const OBB& box, int axisIndex, const float3& outwardNormal, FaceFrame& frame) {
		float sign = dot(outwardNormal, box.axis[axisIndex]) >= 0.0f ? 1.0f : -1.0f;
		frame.axisIndex = axisIndex;
		frame.normal = box.axis[axisIndex] * sign;
		frame.center = box.center + frame.normal * box.half[axisIndex];
		getFaceAxes(box, axisIndex, frame.u, frame.v, frame.extentU, frame.extentV);
	}

	inline int chooseIncidentFaceAxis(const OBB& box, const float3& referenceNormal) {
		int axis = 0;
		float best = -FLT_MAX;

		for (int i = 0; i < 3; ++i) {
			float d = absDot(box.axis[i], referenceNormal);
			if (d > best) {
				best = d;
				axis = i;
			}
		}

		return axis;
	}

	inline void buildIncidentFace(const OBB& box, int axisIndex, const float3& referenceNormal, float3 outVerts[4]) {
		float sign = dot(box.axis[axisIndex], referenceNormal) > 0.0f ? -1.0f : 1.0f;
		float3 faceNormal = box.axis[axisIndex] * sign;
		float3 faceCenter = box.center + faceNormal * box.half[axisIndex];

		float3 u;
		float3 v;
		float extentU;
		float extentV;
		getFaceAxes(box, axisIndex, u, v, extentU, extentV);

		outVerts[0] = faceCenter + u * extentU + v * extentV;
		outVerts[1] = faceCenter - u * extentU + v * extentV;
		outVerts[2] = faceCenter - u * extentU - v * extentV;
		outVerts[3] = faceCenter + u * extentU - v * extentV;
	}

	inline int clipPolygonAgainstPlane(const float3* inVerts, int inCount, const float3& planeNormal, float planeOffset, float3* outVerts) {
		if (inCount <= 0)
			return 0;

		int outCount = 0;
		float3 a = inVerts[inCount - 1];
		float da = dot(planeNormal, a) - planeOffset;

		for (int i = 0; i < inCount; ++i) {
			float3 b = inVerts[i];
			float db = dot(planeNormal, b) - planeOffset;
			bool aInside = da <= PLANE_EPSILON;
			bool bInside = db <= PLANE_EPSILON;

			if (aInside != bInside) {
				float t = 0.0f;
				float denom = da - db;
				if (fabsf(denom) > SAT_AXIS_EPSILON)
					t = clamp(da / denom, 0.0f, 1.0f);

				if (outCount < MAX_POLY_VERTS)
					outVerts[outCount++] = a + (b - a) * t;
			}

			if (bInside && outCount < MAX_POLY_VERTS)
				outVerts[outCount++] = b;

			a = b;
			da = db;
		}

		return outCount;
	}

	inline bool addContact(Rigid* bodyA, Rigid* bodyB, Manifold::Contact* contacts, int& contactCount, float3* contactMidpoints, float3 xA, float3 xB, int featureKey) {
		float3 midpoint = (xA + xB) * 0.5f;

		for (int i = 0; i < contactCount; ++i) {
			float3 d = midpoint - contactMidpoints[i];
			if (lengthSq(d) < CONTACT_MERGE_DIST_SQ)
				return false;
		}

		if (contactCount >= MAX_CONTACTS)
			return false;

		Manifold::FeaturePair feature{};
		feature.key = featureKey;

		Manifold::Contact& c = contacts[contactCount];
		c.feature = feature;
		c.rA = rotate(conjugate(bodyA->positionAng), xA - bodyA->positionLin);
		c.rB = rotate(conjugate(bodyB->positionAng), xB - bodyB->positionLin);
		contactMidpoints[contactCount] = midpoint;
		++contactCount;

		return true;
	}

	inline bool testAxis(const OBB& boxA, const OBB& boxB, const float3& delta, const float3& axis, AxisType type, int indexA, int indexB, SatAxis& best) {
		float lenSq = lengthSq(axis);
		if (lenSq < SAT_AXIS_EPSILON)
			return true;

		float invLen = 1.0f / sqrtf(lenSq);
		float3 n = axis * invLen;
		if (dot(n, delta) < 0.0f)
			n = -n;

		float distance = fabsf(dot(delta, n));

		float rA =
			boxA.half.x * absDot(n, boxA.axis[0]) +
			boxA.half.y * absDot(n, boxA.axis[1]) +
			boxA.half.z * absDot(n, boxA.axis[2]);

		float rB =
			boxB.half.x * absDot(n, boxB.axis[0]) +
			boxB.half.y * absDot(n, boxB.axis[1]) +
			boxB.half.z * absDot(n, boxB.axis[2]);

		float separation = distance - (rA + rB);
		if (separation > 0.0f)
			return false;

		if (!best.valid || separation > best.separation) {
			best.valid = true;
			best.type = type;
			best.normalAB = n;
			best.indexA = indexA;
			best.separation = separation;
			best.indexB = indexB;


		}

		return true;
	}

	inline void supportEdge(const OBB& box, int axisIndex, const float3& dir, float3& edgeA, float3& edgeB) {
		int axis1 = (axisIndex + 1) % 3;
		int axis2 = (axisIndex + 2) % 3;

		float sign1 = dot(dir, box.axis[axis1]) >= 0.0f ? 1.0f : -1.0f;
		float sign2 = dot(dir, box.axis[axis2]) >= 0.0f ? 1.0f : -1.0f;

		float3 edgeCenter = box.center
			+ box.axis[axis1] * (box.half[axis1] * sign1)
			+ box.axis[axis2] * (box.half[axis2] * sign2);

		edgeA = edgeCenter - box.axis[axisIndex] * box.half[axisIndex];
		edgeB = edgeCenter + box.axis[axisIndex] * box.half[axisIndex];
	}

	inline void closestPointsOnSegments(const float3& p0, const float3& p1, const float3& q0, const float3& q1, float3& c0, float3& c1) {
		float3 d1 = p1 - p0;
		float3 d2 = q1 - q0;
		float3 r = p0 - q0;
		float a = dot(d1, d1);
		float e = dot(d2, d2);
		float f = dot(d2, r);

		float s = 0.0f;
		float t = 0.0f;

		if (a <= SAT_AXIS_EPSILON && e <= SAT_AXIS_EPSILON) {
			c0 = p0;
			c1 = q0;
			return;
		}

		if (a <= SAT_AXIS_EPSILON) {
			t = clamp(f / e, 0.0f, 1.0f);
		}
		else {
			float c = dot(d1, r);
			if (e <= SAT_AXIS_EPSILON) {
				s = clamp(-c / a, 0.0f, 1.0f);
			}
			else {
				float b = dot(d1, d2);
				float denom = a * e - b * b;

				if (fabsf(denom) > SAT_AXIS_EPSILON)
					s = clamp((b * f - c * e) / denom, 0.0f, 1.0f);

				t = (b * s + f) / e;

				if (t < 0.0f) {
					t = 0.0f;
					s = clamp(-c / a, 0.0f, 1.0f);
				}
				else if (t > 1.0f) {
					t = 1.0f;
					s = clamp((b - c) / a, 0.0f, 1.0f);
				}
			}
		}

		c0 = p0 + d1 * s;
		c1 = q0 + d2 * t;
	}

	inline int buildFaceManifold(Rigid* bodyA, Rigid* bodyB, const OBB& boxA, const OBB& boxB, bool referenceIsA, int referenceAxis, const float3& normalAB, Manifold::Contact* contacts)
	{
		const OBB& referenceBox = referenceIsA ? boxA : boxB;
		const OBB& incidentBox = referenceIsA ? boxB : boxA;
		float3 referenceOutward = referenceIsA ? normalAB : -normalAB;

		FaceFrame referenceFace{};
		buildFaceFrame(referenceBox, referenceAxis, referenceOutward, referenceFace);

		int incidentAxis = chooseIncidentFaceAxis(incidentBox, referenceFace.normal);

		float3 clip0[MAX_POLY_VERTS];
		float3 clip1[MAX_POLY_VERTS];
		buildIncidentFace(incidentBox, incidentAxis, referenceFace.normal, clip0);
		int count = 4;

		float3 n0 = referenceFace.u;
		float o0 = dot(n0, referenceFace.center) + referenceFace.extentU;
		count = clipPolygonAgainstPlane(clip0, count, n0, o0, clip1);
		if (count == 0)
			return 0;

		float3 n1 = -referenceFace.u;
		float o1 = dot(n1, referenceFace.center) + referenceFace.extentU;
		count = clipPolygonAgainstPlane(clip1, count, n1, o1, clip0);
		if (count == 0)
			return 0;

		float3 n2 = referenceFace.v;
		float o2 = dot(n2, referenceFace.center) + referenceFace.extentV;
		count = clipPolygonAgainstPlane(clip0, count, n2, o2, clip1);
		if (count == 0)
			return 0;

		float3 n3 = -referenceFace.v;
		float o3 = dot(n3, referenceFace.center) + referenceFace.extentV;
		count = clipPolygonAgainstPlane(clip1, count, n3, o3, clip0);
		if (!count)
			return 0;

		int contactCount = 0;
		float3 contactMidpoints[MAX_CONTACTS];
		int featurePrefix = ((referenceIsA ? AXIS_FACE_A : AXIS_FACE_B) << 24) | ((referenceAxis & 0xFF) << 16) | ((incidentAxis & 0xFF) << 8);


		for (int i = 0; i < count && contactCount < MAX_CONTACTS; ++i) {
			float3 pIncident = clip0[i];
			float distance = dot(pIncident - referenceFace.center, referenceFace.normal);
			if (distance > PLANE_EPSILON)
				continue;

			float3 pReference = pIncident - referenceFace.normal * distance;
			float3 xA = referenceIsA ? pReference : pIncident;
			float3 xB = referenceIsA ? pIncident : pReference;

			addContact(bodyA, bodyB, contacts, contactCount, contactMidpoints, xA, xB, featurePrefix | (i & 0xFF));
		}

		if (!contactCount) {
			float3 xA = supportPoint(boxA, normalAB);
			float3 xB = supportPoint(boxB, -normalAB);
			addContact(bodyA, bodyB, contacts, contactCount, contactMidpoints, xA, xB, featurePrefix);
		}

		return contactCount;
	}

	inline int buildEdgeContact(Rigid* bodyA, Rigid* bodyB, const OBB& boxA, const OBB& boxB, int axisA, int axisB, const float3& normalAB, Manifold::Contact* contacts) {
		float3 a0, a1, b0, b1;
		supportEdge(boxA, axisA, normalAB, a0, a1);
		supportEdge(boxB, axisB, -normalAB, b0, b1);

		float3 xA, xB;
		closestPointsOnSegments(a0, a1, b0, b1, xA, xB);

		int contactCount = 0;
		float3 contactMidpoints[MAX_CONTACTS];
		int featureKey = (AXIS_EDGE << 24) | ((axisA & 0xFF) << 8) | (axisB & 0xFF);
		addContact(bodyA, bodyB, contacts, contactCount, contactMidpoints, xA, xB, featureKey);

		if (!contactCount) {
			xA = supportPoint(boxA, normalAB);
			xB = supportPoint(boxB, -normalAB);
			addContact(bodyA, bodyB, contacts, contactCount, contactMidpoints, xA, xB, featureKey);
		}

		return contactCount;
	}

} //namespace

int sphereSphereCollide(Rigid* bodyA, Rigid* bodyB, Manifold::Contact* contacts, float3x3& basisOut) {
	float3 delta = bodyB->positionLin - bodyA->positionLin;
	float rSum = bodyA->radius + bodyB->radius;
	float distSq = lengthSq(delta);
	if (distSq > rSum * rSum)
		return 0;
	if (distSq < SAT_AXIS_EPSILON)
		return 0;
	float dist = sqrtf(distSq);
	float3 normal = delta / dist;

	float3 pointOnA = bodyA->positionLin + normal * bodyA->radius;
	float3 pointOnB = bodyB->positionLin - normal * bodyB->radius;
	float3 contactMidpoints[MAX_CONTACTS];
	int numContacts = 0;
	int featureKey = AXIS_SPHERE << 24;
	addContact(bodyA, bodyB, contacts, numContacts, contactMidpoints, pointOnA, pointOnB, featureKey);
	basisOut = orthonormal(-normal);
	return numContacts;
}

// assume bodyA is the sphere and bodyB is the box
int sphereBoxCollide(Rigid* bodyA, Rigid* bodyB, bool bodyAIsSphere, Manifold::Contact* contacts, float3x3& basisOut) {
	OBB box = makeOBB(bodyAIsSphere ? bodyB : bodyA);

	float3 sphereCenter = bodyAIsSphere ? bodyA->positionLin : bodyB->positionLin;
	float radius = bodyAIsSphere ? bodyA->radius : bodyB->radius;

	float3 delta = sphereCenter - box.center;

	float3 closestPoint = box.center;

	for (int i = 0; i < 3; i++) {
		float dist = dot(delta, box.axis[i]);
		float clamped = clamp(dist, -box.half[i], box.half[i]);
		closestPoint += box.axis[i] * clamped;
	}

	float3 dir = closestPoint - sphereCenter;
	float distSq = lengthSq(dir);

	if (distSq > radius * radius)
		return 0;

	float3 normal;
	
	if (distSq > SAT_AXIS_EPSILON) {
		float dist = sqrtf(distSq);
		normal = dir / dist;
		
	}
	else {
		float minPen = FLT_MAX;
		float3 bestAxis{};

		for (int i = 0; i < 3; i++) {
			float d = dot(delta, box.axis[i]);
			float pen = box.half[i] - fabs(d);
			if (pen < minPen) {
				minPen = pen;
				bestAxis = box.axis[i] * (d > 0.0f ? 1.0f : -1.0f);
			}
		}

		normal = bestAxis;
		
	}

	float3 pointOnSphere = sphereCenter + normal * radius;
	float3 pointOnBox = closestPoint;

	int numContacts = 0;
	int featureKey = (AXIS_CLOSEST << 24) | ((bodyAIsSphere ? 0 : 1) << 16);

	float3 contactMidpoints[MAX_CONTACTS];

	if (bodyAIsSphere) {
		addContact(bodyA, bodyB, contacts, numContacts, contactMidpoints, pointOnSphere, pointOnBox, featureKey);
	}
	else {
		addContact(bodyA, bodyB, contacts, numContacts, contactMidpoints, pointOnBox, pointOnSphere, featureKey);
	}


	basisOut = orthonormal(bodyAIsSphere ? -normal : normal);

	return numContacts;
}

int boxBoxCollide(Rigid* bodyA, Rigid* bodyB, Manifold::Contact* contacts, float3x3& basisOut) {
	OBB boxA = makeOBB(bodyA);
	OBB boxB = makeOBB(bodyB);
	float3 delta = boxB.center - boxA.center;

	SatAxis bestFace{};
	bestFace.separation = -FLT_MAX;
	bestFace.valid = false;

	SatAxis bestEdge{};
	bestEdge.separation = -FLT_MAX;
	bestEdge.valid = false;

	for (int i = 0; i < 3; ++i) {
		if (!testAxis(boxA, boxB, delta, boxA.axis[i], AXIS_FACE_A, i, -1, bestFace))
			return 0;
	}

	for (int i = 0; i < 3; ++i) {
		if (!testAxis(boxA, boxB, delta, boxB.axis[i], AXIS_FACE_B, -1, i, bestFace))
			return 0;
	}

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j)
		{
			float3 axis = cross(boxA.axis[i], boxB.axis[j]);
			if (!testAxis(boxA, boxB, delta, axis, AXIS_EDGE, i, j, bestEdge))
				return 0;
		}
	}

	if (!bestFace.valid)
		return 0;

	SatAxis best = bestFace;
	if (bestEdge.valid) {
		const float edgeRelTol = 0.95f;
		const float edgeAbsTol = 0.01f;
		if (edgeRelTol * bestEdge.separation > bestFace.separation + edgeAbsTol)
			best = bestEdge;
	}
	basisOut = orthonormal(-best.normalAB);

	if (best.type == AXIS_EDGE)
		return buildEdgeContact(bodyA, bodyB, boxA, boxB, best.indexA, best.indexB, best.normalAB, contacts);

	if (best.type == AXIS_FACE_A)
		return buildFaceManifold(bodyA, bodyB, boxA, boxB, true, best.indexA, best.normalAB, contacts);

	return buildFaceManifold(bodyA, bodyB, boxA, boxB, false, best.indexB, best.normalAB, contacts);
}

int Manifold::collide(Rigid* bodyA, Rigid* bodyB, Contact* contacts, float3x3& basisOut)
{
	if (bodyA->shape == Rigid::Shape::Sphere && bodyB->shape == Rigid::Shape::Box)
		return sphereBoxCollide(bodyA, bodyB, true, contacts, basisOut);
	if (bodyA->shape == Rigid::Shape::Box && bodyB->shape == Rigid::Shape::Sphere)
		return sphereBoxCollide(bodyA, bodyB, false, contacts, basisOut);
	if (bodyA->shape == Rigid::Shape::Sphere && bodyB->shape == Rigid::Shape::Sphere)
		return sphereSphereCollide(bodyA, bodyB, contacts, basisOut);
	if (bodyA->shape == Rigid::Shape::Box && bodyB->shape == Rigid::Shape::Box)
		return boxBoxCollide(bodyA, bodyB, contacts, basisOut);
	return 0;
}

