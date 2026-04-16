//https://github.com/savant117/avbd-demo3d/blob/main/source/rigid.cpp
#include "solver.hpp"

Rigid::Rigid(Solver* solver, float3 size, float density, float friction, float3 position, quat rotation, float3 velocity, Shape shape, bool isStatic)
	: solver(solver), forces(0), next(0), positionLin(position), positionAng(rotation), velocityLin(velocity), velocityAng({ 0, 0, 0 }),
	 prevVelocityLin(velocity), size(size), friction(friction), shape(shape), isStatic(isStatic)
{
	next = solver->bodies;
	solver->bodies = this;
	solver->num_bodies++;

	if (shape == Shape::Box) {
		mass = size.x * size.y * size.z * density;
		moment = float3{
			(size.y * size.y + size.z * size.z) / 12.0f * mass,
			(size.x * size.x + size.z * size.z) / 12.0f * mass,
			(size.x * size.x + size.y * size.y) / 12.0f * mass
		};
		radius = length(size * 0.5f);
		
	}
	else if (shape == Shape::Sphere) {
		mass = (4.0f / 3.0f) * 3.14159265359f * size.x * size.x * size.x * density;
		moment = float3{ 0.4f * mass * size.x * size.x, 0.4f * mass * size.x * size.x, 0.4f * mass * size.x * size.x };
		radius = size.x;
	}

}

Rigid::~Rigid() {
	Rigid** p = &solver->bodies;
	while (*p != this) {
		p = &(*p)->next;
	}
	*p = next;
	solver->num_bodies--;
}

bool Rigid::constrainedTo(Rigid* other) const {
	for (Force* f = forces; f != 0; f = f->next) {
		if ((f->bodyA == this && f->bodyB == other) || (f->bodyA == other && f->bodyB == this))
			return true;
	}
	return false;
}
