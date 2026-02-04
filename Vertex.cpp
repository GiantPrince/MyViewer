#include "Vertex.hpp"

#include <array>

static std::array<VkVertexInputBindingDescription, 1> bindings{
	VkVertexInputBindingDescription{
		0,
		sizeof(Vertex),
		VK_VERTEX_INPUT_RATE_VERTEX
	}
};

static std::array<VkVertexInputAttributeDescription, 4> attributes{
	VkVertexInputAttributeDescription{
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, Position)
	},
	VkVertexInputAttributeDescription{
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, Normal)
	},
	VkVertexInputAttributeDescription{
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = offsetof(Vertex, Tangent)
	},
	VkVertexInputAttributeDescription{
		.location = 3,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = offsetof(Vertex, TexCoord)
	}
};

const VkPipelineVertexInputStateCreateInfo Vertex::array_input_state{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size()),
	.pVertexBindingDescriptions = bindings.data(),
	.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
	.pVertexAttributeDescriptions = attributes.data()
};

