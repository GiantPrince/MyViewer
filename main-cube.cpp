
#include <vector>
#include <cassert>
#include <cmath>
#include <string>
#include <stdexcept>
#include <iostream>


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.hpp"


struct vec3 {
	float x, y, z;
	vec3():x(0), y(0), z(0) {}
	vec3(float x, float y, float z):x(x), y(y), z(z) {}

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

vec3 decode(unsigned char* rgbe) {
	if (rgbe[0] == 0 && rgbe[1] == 0 && rgbe[2] == 0 && rgbe[3] == 0) {
		return vec3();
	}

	uint32_t e = rgbe[3];
	uint32_t b = rgbe[2];
	uint32_t g = rgbe[1];
	uint32_t r = rgbe[0];

	float scale = std::ldexp(1.0f, e - 128 - 8);
	float fr = (r + 0.5f) * scale;
	float fg = (g + 0.5f) * scale;
	float fb = (b + 0.5f) * scale;
	return vec3(fr, fg, fb);
}

void encode(const vec3& rgb, std::vector<unsigned char> &image, size_t index) {
	float max_c = std::max(rgb.x, std::max(rgb.y, rgb.z));
	int shared_exp;
	std::frexp(max_c, &shared_exp);
	uint32_t biased_exp = std::max(0, std::min(255, shared_exp + 128));
	float denom = std::ldexp(1.0f, biased_exp - 128 - 8);
	uint32_t r = (uint32_t)std::min(255.0f, std::round(rgb.x / denom));
	uint32_t g = (uint32_t)std::min(255.0f, std::round(rgb.y / denom));
	uint32_t b = (uint32_t)std::min(255.0f, std::round(rgb.z / denom));
	image[index + 3] = static_cast<unsigned char>(biased_exp);
	image[index + 2] = static_cast<unsigned char>(b);
	image[index + 1] = static_cast<unsigned char>(g);
	image[index + 0] = static_cast<unsigned char>(r);

}

struct Texel {
	vec3 dir;
	vec3 radiance;
	float dOmega;
};

vec3 computeDirection(size_t face, float u, float v) {
	switch (face) {
	case 0: return normalize(vec3(1, -u, -v));
	case 1: return normalize(vec3(-1, -u, v));
	case 2: return normalize(vec3(v, 1, u));
	case 3: return normalize(vec3(v, -1, -u));
	case 4: return normalize(vec3(v, -u, 1));
	case 5: return normalize(vec3(-v, -u, -1));	
	default: assert(0);  return vec3();
	}
}

std::vector<unsigned char> preconvolvedLambertian(
	unsigned char* image,
	uint32_t width,
	uint32_t height
) {
	uint32_t faceHeight = height / 6;
	assert(faceHeight == width);

	std::vector<Texel> texels;
	texels.reserve(width * height);

	size_t size = width;

	float du = 2.0f / size;
	float dv = 2.0f / size;
	
	for (size_t face = 0; face < 6; face++) {
		std::cout << ".";
		for (size_t i = 0; i < size; i++) {
			for (size_t j = 0; j < size; j++) {
				size_t global_i = face * size + i;
				size_t global_index = global_i * size + j;

				float u = (2.0f * (i + 0.5f) / size) - 1.0f;
				float v = (2.0f * (j + 0.5f) / size) - 1.0f;

				vec3 dir = computeDirection(face, u, v);
				vec3 radiance = decode(&image[global_index * 4]);

				float denom = pow(1.0f + u * u + v * v, 1.5f);
				float dOmega = (du * dv) / denom;
				texels.emplace_back(dir, radiance, dOmega);
			}
		}
	}	

	stbi_image_free(image);

	size_t outputSize = 16;
	std::vector<unsigned char> newImage(outputSize * outputSize * 4 * 6, 0);
	

	for (size_t face = 0; face < 6; face++) {
		std::cout << "/" << face;
		for (size_t i = 0; i < outputSize; i++) {
			for (size_t j = 0; j < outputSize; j++) {
				size_t global_i = face * outputSize + i;
				size_t global_index = global_i * outputSize + j;

				float u = (2.0f * (i + 0.5f) / outputSize) - 1.0f;
				float v = (2.0f * (j + 0.5f) / outputSize) - 1.0f;

				vec3 normal = computeDirection(face, u, v);

				vec3 irradiance;

				for (size_t t = 0; t < width * height; t++) {
					float ct = dot(normal, texels[t].dir);
					if (ct > 0.0f) {
						irradiance += texels[t].radiance * (ct * texels[t].dOmega);
					}
				}
				irradiance = irradiance * (1.0f / float(M_PI));
				encode(irradiance, newImage, global_index * 4);				
			}
		}
	}
	return newImage;
	
}

std::pair<std::string, std::string> parse(int argc, char* argv[]) {
	assert(argc == 4 && "Should have exactly 3 arguments");
	assert(std::string(argv[2]) == "--lambertian");
	return { argv[1], argv[3] };	
}

int main(int argc, char* argv[]) {
	auto [infile, outfile] = parse(argc, argv);
	assert(infile.ends_with(".png"));
	assert(outfile.ends_with(".png"));
	int width, height, channel;
	unsigned char* image = stbi_load(infile.c_str(), &width, &height, &channel, 4);
	if (image == nullptr) {
		throw std::runtime_error("failed to load file " + infile);
	}

	auto newImage = preconvolvedLambertian(image, width, height);

	int stride = 16 * 4;

	if (!stbi_write_png(
		outfile.c_str(),
		16,
		16 * 6,
		4,          
		newImage.data(),
		stride))
	{
		std::cout << std::endl;
		throw std::runtime_error("failed to write file " + outfile);
	}	
		
	std::cout << std::endl;
}