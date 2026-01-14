#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

struct PosColVertex {
	struct { float x, y, z; } Position;
	struct { float r, g, b, a; } Color;

	static const VkPipelineVertexInputStateCreateInfo array_input_state;
};

static_assert(sizeof(PosColVertex) == 4 * 3 + 4 * 4, "PosColVertex is packed.");

