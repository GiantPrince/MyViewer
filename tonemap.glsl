#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

layout(push_constant) uniform Push {
    float exposure;
	int tone_operator;
};

vec3 expose(vec3 color) {
	return color * pow(2.0, exposure);
}

vec3 linear_tonemap(vec3 color) {
	return clamp(color, 0, 1);
}

vec3 reinhard_tonemap(vec3 color) {	
	return color / (1 + color);
}

vec3 tonemap(vec3 color) {
	vec3 exposed = expose(color);
	vec3 tonemapped;
	if (tone_operator == 0) {
		tonemapped = linear_tonemap(exposed);
	}
	else if (tone_operator == 1) {
		tonemapped = reinhard_tonemap(exposed);
	}
	else {
		tonemapped = exposed;
	}
	return tonemapped;
}

#endif