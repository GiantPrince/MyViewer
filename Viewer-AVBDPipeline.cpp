#include "Viewer.hpp"

#include "Helpers.hpp"

#include "VK.hpp"


static uint32_t avbd_code[] =
#include"spv/avbd.comp.inl"
;

static uint32_t broad_collision_code[] =
#include "spv/broad-collision.comp.inl"
;

static uint32_t precise_collision_code[] =
#include "spv/precise-collision.comp.inl"
;

static uint32_t graph_color_code[] =
#include "spv/graph-color.comp.inl"
;

static uint32_t main_loop_init_code[] =
#include"spv/main-loop-init.comp.inl"
;

static uint32_t main_loop_code[] =
#include"spv/main-loop.comp.inl"
;

static uint32_t main_loop_copy_back_code[] =
#include"spv/main-loop-copy-back.comp.inl"
;

static uint32_t main_loop_dual_code[] =
#include"spv/main-loop-dual.comp.inl"
;

static uint32_t velocity_update_code[] =
#include"spv/velocity-update.comp.inl"
;

void Viewer::AVBDPipeline::create(RTG& rtg, VkRenderPass render_pass, uint32_t subpass)
{
	VkShaderModule avbd_module = rtg.helpers.create_shader_module(avbd_code);
	VkShaderModule broad_collision_module = rtg.helpers.create_shader_module(broad_collision_code);
	VkShaderModule precise_collision_module = rtg.helpers.create_shader_module(precise_collision_code);
	VkShaderModule graph_color_module = rtg.helpers.create_shader_module(graph_color_code);
	VkShaderModule main_loop_init_module = rtg.helpers.create_shader_module(main_loop_init_code);
	VkShaderModule main_loop_module = rtg.helpers.create_shader_module(main_loop_code);
	VkShaderModule main_loop_copy_back_module = rtg.helpers.create_shader_module(main_loop_copy_back_code);
	VkShaderModule main_loop_dual_module = rtg.helpers.create_shader_module(main_loop_dual_code);
	VkShaderModule velocity_update_module = rtg.helpers.create_shader_module(velocity_update_code);

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

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set0_Rigids));
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

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set1_UpdatedRigids));
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

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set2_Manifolds));
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

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set3_Colors));
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

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set4_Counter));
	}

	{

		VkPushConstantRange range{
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset = 0,
			.size = sizeof(Push)
		};

		std::array<VkDescriptorSetLayout, 5> layouts{
			set0_Rigids,
			set1_UpdatedRigids,
			set2_Manifolds,
			set3_Colors,
			set4_Counter
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

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = avbd_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = broad_collision_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &broad_collision_handle));
	}

	
	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = precise_collision_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &precise_collision_handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = graph_color_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &graph_color_handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = main_loop_init_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &main_loop_init_handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = main_loop_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &main_loop_handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = main_loop_copy_back_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &main_loop_copy_back_handle));
	}

	{
		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = main_loop_dual_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &main_loop_dual_handle));
	}

	{

		VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = velocity_update_module,
			.pName = "main"
		};

		VkComputePipelineCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = layout
		};

		VK(vkCreateComputePipelines(rtg.device, nullptr, 1, &create_info, nullptr, &velocity_update_handle));
	}


	vkDestroyShaderModule(rtg.device, avbd_module, nullptr);	
	vkDestroyShaderModule(rtg.device, broad_collision_module, nullptr);
	vkDestroyShaderModule(rtg.device, precise_collision_module, nullptr);
	vkDestroyShaderModule(rtg.device, graph_color_module, nullptr);
	vkDestroyShaderModule(rtg.device, main_loop_init_module, nullptr);
	vkDestroyShaderModule(rtg.device, main_loop_module, nullptr);
	vkDestroyShaderModule(rtg.device, main_loop_copy_back_module, nullptr);
	vkDestroyShaderModule(rtg.device, main_loop_dual_module, nullptr);
	vkDestroyShaderModule(rtg.device, velocity_update_module, nullptr);
	
}

void Viewer::AVBDPipeline::destroy(RTG& rtg)
{
	if (handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, handle, nullptr);
		handle = VK_NULL_HANDLE;
	}
	if (broad_collision_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, broad_collision_handle, nullptr);
		broad_collision_handle = VK_NULL_HANDLE;
	}

	if (precise_collision_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, precise_collision_handle, nullptr);
		precise_collision_handle = VK_NULL_HANDLE;
	}

	if (graph_color_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, graph_color_handle, nullptr);
		graph_color_handle = VK_NULL_HANDLE;
	}

	if (main_loop_init_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, main_loop_init_handle, nullptr);
		main_loop_init_handle = VK_NULL_HANDLE;
	}

	if (main_loop_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, main_loop_handle, nullptr);
		main_loop_handle = VK_NULL_HANDLE;
	}

	if (main_loop_copy_back_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, main_loop_copy_back_handle, nullptr);
		main_loop_copy_back_handle = VK_NULL_HANDLE;
	}

	if (main_loop_dual_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, main_loop_dual_handle, nullptr);
		main_loop_dual_handle = VK_NULL_HANDLE;
	}

	if (velocity_update_handle != VK_NULL_HANDLE) {
		vkDestroyPipeline(rtg.device, velocity_update_handle, nullptr);
		velocity_update_handle = VK_NULL_HANDLE;
	}

	

	if (layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(rtg.device, layout, nullptr);
		layout = VK_NULL_HANDLE;
	}

	if (set0_Rigids != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_Rigids, nullptr);
		set0_Rigids = VK_NULL_HANDLE;
	}

	if (set1_UpdatedRigids != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set1_UpdatedRigids, nullptr);
		set1_UpdatedRigids = VK_NULL_HANDLE;
	}

	if (set2_Manifolds != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set2_Manifolds, nullptr);
		set2_Manifolds = VK_NULL_HANDLE;
	}

	if (set3_Colors != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set3_Colors, nullptr);
		set3_Colors = VK_NULL_HANDLE;
	}

	if (set4_Counter != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set4_Counter, nullptr);
		set4_Counter = VK_NULL_HANDLE;
	}
}
