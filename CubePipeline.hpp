#pragma once
#include "RTG.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

struct CubePipeline {
	// descriptor sets
	VkDescriptorSetLayout set0_inputImageCube = VK_NULL_HANDLE;
	VkDescriptorSetLayout set1_outputImageCube = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;	

	//push constants
	struct Push {
		float roughness;
		uint32_t faceSize;
		uint32_t numOfSamples;
		uint32_t mipmapLevel;
	};
	
	VkPipelineLayout layout = VK_NULL_HANDLE;

	// no vertex bindings
	VkPipeline handle = VK_NULL_HANDLE;
	void create(RTG&);
	void destroy(RTG&);
};