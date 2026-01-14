#include "PosColVertex.hpp"

#include <array>

static std::array<VkVertexInputBindingDescription, 1> bindings{
	VkVertexInputBindingDescription{
		0,
		sizeof(PosColVertex),
		VK_VERTEX_INPUT_RATE_VERTEX
	}
};

static std::array<VkVertexInputAttributeDescription, 2> attributes{
	VkVertexInputAttributeDescription{
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(PosColVertex, Position)
	},
	VkVertexInputAttributeDescription{
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = offsetof(PosColVertex, Color)
	}
};

const VkPipelineVertexInputStateCreateInfo PosColVertex::array_input_state{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size()),
	.pVertexBindingDescriptions = bindings.data(),
	.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
	.pVertexAttributeDescriptions = attributes.data()
};

