#include "CubePipeline.hpp"

static uint32_t compute_code[] =
#include"spv/cube.comp.inl"
;

static uint32_t lut_code[] =
#include"spv/lut.comp.inl"
;

void CubePipeline::create(RTG& rtg) {
	VkShaderModule compute_module = rtg.helpers.create_shader_module(compute_code);
	VkShaderModule lut_module = rtg.helpers.create_shader_module(lut_code);

	if (rtg.configuration.cube_util_mode != RTG::Configuration::CubeUtilMode::LUT)
	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings{
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			}
		};

		VkDescriptorSetLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data()
		};

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set0_inputImageCube));

	}

	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings{
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			}
		};

		VkDescriptorSetLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data()
		};

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set1_outputImageCube));
	}

	if (rtg.configuration.cube_util_mode != RTG::Configuration::CubeUtilMode::LUT)
	{
		VkPushConstantRange range{
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset = 0,
			.size = sizeof(Push)
		};

		std::array<VkDescriptorSetLayout, 2> layouts{
			set0_inputImageCube,
			set1_outputImageCube
		};

		VkPipelineLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data(),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &range
		};

		VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
	}
	else {		

		std::array<VkDescriptorSetLayout, 1> layouts{			
			set1_outputImageCube
		};

		VkPipelineLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data(),			
		};

		VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
	}

	if (rtg.configuration.cube_util_mode != RTG::Configuration::CubeUtilMode::LUT)
	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = compute_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &handle));
	}
	else {
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = lut_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &handle));
	}

	vkDestroyShaderModule(rtg.device, compute_module, nullptr);
	vkDestroyShaderModule(rtg.device, lut_module, nullptr);

}

void CubePipeline::destroy(RTG& rtg) {
	if (handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, handle, nullptr);
		handle = VK_NULL_HANDLE;
	}

	if (layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(rtg.device, layout, nullptr);
		layout = VK_NULL_HANDLE;
	}

	if (set0_inputImageCube != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_inputImageCube, nullptr);
		set0_inputImageCube = VK_NULL_HANDLE;
	}

	if (set1_outputImageCube != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set1_outputImageCube, nullptr);
		set1_outputImageCube = VK_NULL_HANDLE;
	}
}

