#include "Physics.hpp"

const Physics::vec3 Physics::gravity = Physics::vec3{ 0.0f, - 9.81f * 0.001f , 0.0f};

Physics::vec3 Physics::apply_gravity(const vec3& velocity, float dt, const vec3& g)
{
	return velocity + g * dt;
}

Physics::vec3 Physics::apply_velocity(const vec3& position, const vec3& velocity, float dt)
{
	return position + velocity * dt;
}




