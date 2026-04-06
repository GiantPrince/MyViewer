
#pragma once
#include <cmath>

struct vec3 {
	float x, y, z;
	vec3() :x(0), y(0), z(0) {}
	vec3(float x, float y, float z) :x(x), y(y), z(z) {}

	vec3 operator+(const vec3& other) const {
		return vec3(x + other.x, y + other.y, z + other.z);
	}
	vec3 operator*(const vec3& other) const {
		return vec3(x * other.x, y * other.y, z * other.z);
	}
	vec3 operator*(float f) const {
		return vec3(x * f, y * f, z * f);
	}
	vec3& operator+=(const vec3& other) {
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}
};

float dot(const vec3& a, const vec3& b) {
	vec3 c = a * b;
	return c.x + c.y + c.z;
}

vec3 normalize(const vec3& a) {
	float l = std::sqrt(dot(a, a));
	return vec3(a.x / l, a.y / l, a.z / l);
}