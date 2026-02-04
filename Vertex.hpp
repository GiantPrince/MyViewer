
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

struct Vertex {
	struct { float x, y, z; } Position;
	struct { float x, y, z; } Normal;
	struct { float x, y, z, w; } Tangent;
	struct { float s, t; } TexCoord;

	static const VkPipelineVertexInputStateCreateInfo array_input_state;
};

static_assert(sizeof(Vertex) == 3 * 4 + 3 * 4 + 4 * 4 + 2 * 4, "Vertex is packed.");