#include "Viewer.hpp"

#include "VK.hpp"

#include "Timer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"


#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stack>



Viewer::Viewer(RTG& rtg_) : rtg(rtg_) {
	//refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);
	depth_format = rtg.helpers.find_image_format(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32 },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	{
		//command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value()
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	{// render pass

		// attachment
		std::array<VkAttachmentDescription, 2> attachments{
			VkAttachmentDescription{
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = rtg.present_layout
			},
			VkAttachmentDescription{
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			}
		};

		VkAttachmentReference color_attachment_ref{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		};

		VkAttachmentReference depth_attachment_ref{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref
		};

		std::array<VkSubpassDependency, 2> dependencies{
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT

			}
		};



		VkRenderPassCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data()
		};

		VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass));
	}
	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{
		uint32_t per_workspace = static_cast<uint32_t>(rtg.workspaces.size());

		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace
			}
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = per_workspace * 3,
			.poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data()
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));

	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace& workspace : workspaces) {
		//refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
		{	// command buffer
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1
			};

			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));

		}
		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);

		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));

		}

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);

		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));
		}

		{
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));
		}

		{
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size
			};

			std::array< VkWriteDescriptorSet, 2> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info
				}
			};

			vkUpdateDescriptorSets(
				rtg.device,
				static_cast<uint32_t>(writes.size()),
				writes.data(),
				0,
				nullptr
			);

		}

	}

	// load meshes:
	if (!rtg.configuration.indexed)
	{
		std::vector<Vertex> vertices = load_mesh_vertices();

		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {
			construct_bounding_boxes(vertices);
		}

		size_t bytes = vertices.size() * sizeof(Vertex);
		mesh_vertex_buffer = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_buffer(
			vertices.data(),
			bytes,
			mesh_vertex_buffer
		);
	}
	else {
		auto [vertices, indices] = load_mesh_vertices_indexed();

		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {
			construct_bounding_boxes(vertices);
		}

		size_t bytes = vertices.size() * sizeof(Vertex);
		mesh_vertex_buffer = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
		size_t indices_bytes = sizeof(uint32_t) * indices.size();
		mesh_indices_buffer = rtg.helpers.create_buffer(
			indices_bytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_buffer(
			vertices.data(),
			bytes,
			mesh_vertex_buffer
		);
		rtg.helpers.transfer_to_buffer(
			indices.data(),
			indices_bytes,
			mesh_indices_buffer
		);
	}

	{
		load_textures();
	}

	{
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const& image : textures) {
			VkImageViewCreateInfo create_info{};
			if (image.format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
				create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				create_info.flags = 0;
				create_info.image = image.handle;
				create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				create_info.format = image.format;
				create_info.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 6
				};					
			}
			else {
				create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				create_info.flags = 0;
				create_info.image = image.handle;
				create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				create_info.format = image.format;
				create_info.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				};
			}			
			VkImageView image_view = VK_NULL_HANDLE;
			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &image_view));
			texture_views.emplace_back(image_view);
		}

		assert(texture_views.size() == textures.size());
	}

	{
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f,
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE
		};

		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
	}

	{
		uint32_t per_texture = static_cast<uint32_t>(textures.size());
		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_texture
			}
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = 1 * per_texture,
			.poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data()
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));

	}

	{
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set2_TEXTURE
		};

		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
		for (VkDescriptorSet& texture_descriptor : texture_descriptors) {
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &texture_descriptor));
		}


		std::vector<VkDescriptorImageInfo> infos(textures.size());
		std::vector<VkWriteDescriptorSet> writes(textures.size());

		for (Helpers::AllocatedImage& image : textures) {
			size_t i = &image - &textures[0];

			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};

			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i]
			};


		}

		vkUpdateDescriptorSets(rtg.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	}

	// camera
	if (rtg.configuration.camera_name != "") {
		camera_mode = CameraMode::Scene;
		previous_camera_mode = CameraMode::Scene;
	}
	else {
		camera_mode = CameraMode::Free;
		previous_camera_mode = CameraMode::Free;
	}

	// profiling
	if (rtg.configuration.profile) {
		VkQueryPoolCreateInfo queryPoolInfo{
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.queryType = VK_QUERY_TYPE_TIMESTAMP,
			.queryCount = static_cast<uint32_t>(rtg.workspaces.size() * 2),			
		};
		vkCreateQueryPool(rtg.device, &queryPoolInfo, nullptr, &query_pool);
		timestamp_period = rtg.timestamp_period;
	}


}

Viewer::~Viewer() {

	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (rtg.configuration.profile && query_pool != VK_NULL_HANDLE) {
		vkDestroyQueryPool(rtg.device, query_pool, nullptr);
		query_pool = VK_NULL_HANDLE;
	}

	if (texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;
		texture_descriptors.clear();
	}

	if (texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	for (VkImageView& view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}

	texture_views.clear();

	for (auto& texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();


	rtg.helpers.destroy_buffer(std::move(object_vertices));
	rtg.helpers.destroy_buffer(std::move(mesh_vertex_buffer));
	rtg.helpers.destroy_buffer(std::move(mesh_indices_buffer));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace& workspace : workspaces) {
		//refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);

		if (workspace.command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}
		if (workspace.line_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.line_vertices_src));
		}

		if (workspace.line_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.line_vertices));
		}

		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}

		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}

		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}

		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
	}

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = VK_NULL_HANDLE;
	}

	workspaces.clear();

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	if (command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}
	//refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
	if (render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

void Viewer::on_swapchain(RTG& rtg_, RTG::SwapchainEvent const& swapchain) {
	//[re]create framebuffers:
	//refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);

	{
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));

	}

	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain_framebuffers.size(); i++) {
		std::array<VkImageView, 2> attachments{
			swapchain.image_views[i],
			swapchain_depth_image_view
		};

		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1
		};

		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));

	}
}

void Viewer::destroy_framebuffers() {
	//refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	for (VkFramebuffer& framebuffer : swapchain_framebuffers) {
		if (framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
			framebuffer = VK_NULL_HANDLE;
		}
	}

	swapchain_framebuffers.clear();
	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);

	swapchain_depth_image_view = VK_NULL_HANDLE;
	rtg.helpers.destroy_image(std::move(swapchain_depth_image));
}


bool Viewer::is_mesh_in_frustum(const std::string& name, const BoundingBox& box, const mat4& WORLD_FROM_LOCAL)
{

	float near_y = 0, near_x = 0, far_y = 0, far_x = 0;
	float near_z = 0, far_z = 0;

	if (camera_mode == CameraMode::Scene || (camera_mode == CameraMode::Debug && previous_camera_mode == CameraMode::Scene)) {

		near_y = std::tan(scene_camera.fov / 2.0f) * scene_camera.near;
		near_x = near_y * scene_camera.aspect;

		far_y = std::tan(scene_camera.fov / 2.0f) * scene_camera.far;
		far_x = far_y * scene_camera.aspect;

		near_z = scene_camera.near;
		far_z = scene_camera.far;

	}
	else if (camera_mode == CameraMode::Free || (camera_mode == CameraMode::Debug && previous_camera_mode == CameraMode::Free)) {

		near_y = std::tan(free_camera.fov / 2.0f) * free_camera.near - 0.01f;
		near_x = near_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height - 0.01f;

		far_y = std::tan(free_camera.fov / 2.0f) * free_camera.far - 0.01f;
		far_x = far_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height - 0.01f;

		near_z = free_camera.near;
		far_z = free_camera.far;

	}
	else {
		assert(false && "invalid camera mode");
	}

	std::array<vec4, 8> corners{
		vec4{ box.min_x, box.min_y, box.min_z, 1.0f },
		vec4{ box.max_x, box.min_y, box.min_z, 1.0f },
		vec4{ box.min_x, box.max_y, box.min_z, 1.0f },
		vec4{ box.max_x, box.max_y, box.min_z, 1.0f },
		vec4{ box.min_x, box.min_y, box.max_z, 1.0f },
		vec4{ box.max_x, box.min_y, box.max_z, 1.0f },
		vec4{ box.min_x, box.max_y, box.max_z, 1.0f },
		vec4{ box.max_x, box.max_y, box.max_z, 1.0f },
	};

	mat4 view_from_world;
	if (camera_mode == CameraMode::Scene || (camera_mode == CameraMode::Debug && previous_camera_mode == CameraMode::Scene)) {

		view_from_world = look_at(
			scene_camera.eye_x, scene_camera.eye_y, scene_camera.eye_z,
			scene_camera.eye_x + scene_camera.forward_x, scene_camera.eye_y + scene_camera.forward_y, scene_camera.eye_z + scene_camera.forward_z,
			scene_camera.up_x, scene_camera.up_y, scene_camera.up_z
		);
	}
	else if (camera_mode == CameraMode::Free || (camera_mode == CameraMode::Debug && previous_camera_mode == CameraMode::Free)) {
		view_from_world = orbit(free_camera.target_x, free_camera.target_y, free_camera.target_z, free_camera.azimuth, free_camera.elevation, free_camera.radius);
	}
	else {
		assert(false && "invalid camera mode");
		view_from_world = mat4{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
	}

	auto m = view_from_world * WORLD_FROM_LOCAL;
	

	for (int i = 0; i < corners.size(); i++) {
		corners[i] = m * corners[i];
	}

	float near_top_left_x = -near_x;
	float near_top_left_y = near_y;
	float near_top_left_z = -near_z;

	float near_top_right_x = near_x;
	float near_top_right_y = near_y;
	float near_top_right_z = -near_z;

	float near_bottom_left_x = near_x;
	float near_bottom_left_y = near_y;
	float near_bottom_left_z = -near_z;

	float near_bottom_right_x = near_x;
	float near_bottom_right_y = near_y;
	float near_bottom_right_z = -near_z;

	float far_top_left_x = -far_x;
	float far_top_left_y = far_y;
	float far_top_left_z = -far_z;

	float far_top_right_x = far_x;
	float far_top_right_y = far_y;
	float far_top_right_z = -far_z;

	float far_bottom_left_x = -far_x;
	float far_bottom_left_y = -far_y;
	float far_bottom_left_z = -far_z;

	float far_bottom_right_x = far_x;
	float far_bottom_right_y = -far_y;
	float far_bottom_right_z = -far_z;


	std::array<vec4, 8> frustum_corners{
		vec4{ near_top_left_x,     near_top_left_y,     near_top_left_z,     1.0f },
		vec4{ near_top_right_x,    near_top_right_y,    near_top_right_z,    1.0f },
		vec4{ near_bottom_left_x,  near_bottom_left_y,  near_bottom_left_z,  1.0f },
		vec4{ near_bottom_right_x, near_bottom_right_y, near_bottom_right_z, 1.0f },

		vec4{ far_top_left_x,      far_top_left_y,      far_top_left_z,      1.0f },
		vec4{ far_top_right_x,     far_top_right_y,     far_top_right_z,     1.0f },
		vec4{ far_bottom_left_x,   far_bottom_left_y,   far_bottom_left_z,   1.0f },
		vec4{ far_bottom_right_x,  far_bottom_right_y,  far_bottom_right_z,  1.0f }
	};

	const vec4 u{ 0, 1, 0, 0 };
	const vec4 r{ 1, 0, 0, 0 };

	const std::array<vec4, 3> box_axes{
		corners[1] - corners[0],
		corners[2] - corners[0],
		corners[4] - corners[0]
	};

	const std::array<vec4, 4> frustum_edges{
		vec4{-near_x, near_y, near_z, 0},
		vec4{near_x, near_y, near_z, 0},
		vec4{near_x, -near_y, near_z, 0},
		vec4{-near_x, -near_y, near_z, 0},
	};

	std::array<vec4, 26> axes{
		vec4{ 0, 0, 1, 0 },
		vec4{ near_z, 0, -near_x, 0 },
		vec4{ -near_z, 0, -near_x, 0 },
		vec4{ 0, -near_z, -near_y, 0 },
		vec4{ 0, -near_z, -near_y, 0 },
		box_axes[0],
		box_axes[1],
		box_axes[2],
		cross(box_axes[0], u),
		cross(box_axes[1], u),
		cross(box_axes[2], u),
		cross(box_axes[0], r),
		cross(box_axes[1], r),
		cross(box_axes[2], r),
		cross(box_axes[0], frustum_edges[0]),
		cross(box_axes[0], frustum_edges[1]),
		cross(box_axes[0], frustum_edges[2]),
		cross(box_axes[0], frustum_edges[3]),
		cross(box_axes[1], frustum_edges[0]),
		cross(box_axes[1], frustum_edges[1]),
		cross(box_axes[1], frustum_edges[2]),
		cross(box_axes[1], frustum_edges[3]),
		cross(box_axes[2], frustum_edges[0]),
		cross(box_axes[2], frustum_edges[1]),
		cross(box_axes[2], frustum_edges[2]),
		cross(box_axes[2], frustum_edges[3]),
	};

	for (uint32_t i = 0; i < axes.size(); i++) {
		const auto& axis = axes[i];
		

		if (!sat_intersect(corners, frustum_corners, axis)) {
			
			return false;
		}
	}

	return true;
}

bool Viewer::sat_intersect(const std::array<vec4, 8>& box_corners, const std::array<vec4, 8>& frustum_corners, const vec4& axis)
{
	float box_min = std::numeric_limits<float>::max();
	float box_max = std::numeric_limits<float>::lowest();

	float frustum_min = std::numeric_limits<float>::max();
	float frustum_max = std::numeric_limits<float>::lowest();

	for (const auto& corner : box_corners) {
		float dot_value = dot(corner, axis);
		box_min = std::min(box_min, dot_value);
		box_max = std::max(box_max, dot_value);
	}

	for (const auto& corner : frustum_corners) {
		float dot_value = dot(corner, axis);
		frustum_min = std::min(frustum_min, dot_value);
		frustum_max = std::max(frustum_max, dot_value);
	}

	return !(box_min > frustum_max || box_max < frustum_min);

}

void Viewer::draw_frustum()
{
	float right_x, right_y, right_z;
	float up_x, up_y, up_z;
	float out_x, out_y, out_z;
	float cam_x, cam_y, cam_z;

	float near_y, near_x, far_y, far_x;
	float near_z, far_z;

	if (previous_camera_mode == CameraMode::Free) {
		float ca = std::cos(free_camera.azimuth);
		float sa = std::sin(free_camera.azimuth);
		float ce = std::cos(free_camera.elevation);
		float se = std::sin(free_camera.elevation);

		//compute right direction
		right_x = -sa;
		right_y = ca;
		right_z = 0.0f;

		//compute up direction
		up_x = -se * ca;
		up_y = -se * sa;
		up_z = ce;

		//compute out direction
		out_x = ce * ca;
		out_y = ce * sa;
		out_z = se;

		cam_x = free_camera.target_x + free_camera.radius * out_x;
		cam_y = free_camera.target_y + free_camera.radius * out_y;
		cam_z = free_camera.target_z + free_camera.radius * out_z;

		near_y = std::tan(free_camera.fov / 2.0f) * free_camera.near - 0.01f;
		near_x = near_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height - 0.01f;

		far_y = std::tan(free_camera.fov / 2.0f) * free_camera.far - 0.01f;
		far_x = far_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height - 0.01f;

		near_z = free_camera.near;
		far_z = free_camera.far;
	}
	else if (previous_camera_mode == CameraMode::Scene) {
		right_x = scene_camera.forward_y * scene_camera.up_z - scene_camera.up_y * scene_camera.forward_z;
		right_y = scene_camera.forward_z * scene_camera.up_x - scene_camera.forward_x * scene_camera.up_z;
		right_z = scene_camera.forward_x * scene_camera.up_y - scene_camera.forward_y * scene_camera.up_x;

		float len_right = std::sqrt(right_x * right_x + right_y * right_y + right_z * right_z);
		right_x /= len_right;
		right_y /= len_right;
		right_z /= len_right;

		up_x = scene_camera.up_x;
		up_y = scene_camera.up_y;
		up_z = scene_camera.up_z;

		out_x = -scene_camera.forward_x;
		out_y = -scene_camera.forward_y;
		out_z = -scene_camera.forward_z;

		cam_x = scene_camera.eye_x;
		cam_y = scene_camera.eye_y;
		cam_z = scene_camera.eye_z;

		near_y = std::tan(scene_camera.fov / 2.0f) * scene_camera.near;
		near_x = near_y * scene_camera.aspect;

		far_y = std::tan(scene_camera.fov / 2.0f) * scene_camera.far;
		far_x = far_y * scene_camera.aspect;

		near_z = scene_camera.near;
		far_z = scene_camera.far;
	}
	else {
		assert(false && "unhandled camera mode");
		return;
	}



	float near_top_left_x = cam_x + (-out_x * near_z) + (up_x * near_y) - (right_x * near_x);
	float near_top_left_y = cam_y + (-out_y * near_z) + (up_y * near_y) - (right_y * near_x);
	float near_top_left_z = cam_z + (-out_z * near_z) + (up_z * near_y) - (right_z * near_x);

	float near_top_right_x = cam_x + (-out_x * near_z) + (up_x * near_y) + (right_x * near_x);
	float near_top_right_y = cam_y + (-out_y * near_z) + (up_y * near_y) + (right_y * near_x);
	float near_top_right_z = cam_z + (-out_z * near_z) + (up_z * near_y) + (right_z * near_x);

	float near_bottom_left_x = cam_x + (-out_x * near_z) + (-up_x * near_y) - (right_x * near_x);
	float near_bottom_left_y = cam_y + (-out_y * near_z) + (-up_y * near_y) - (right_y * near_x);
	float near_bottom_left_z = cam_z + (-out_z * near_z) + (-up_z * near_y) - (right_z * near_x);

	float near_bottom_right_x = cam_x + (-out_x * near_z) + (-up_x * near_y) + (right_x * near_x);
	float near_bottom_right_y = cam_y + (-out_y * near_z) + (-up_y * near_y) + (right_y * near_x);
	float near_bottom_right_z = cam_z + (-out_z * near_z) + (-up_z * near_y) + (right_z * near_x);

	float far_top_left_x = cam_x + (-out_x * far_z) + (up_x * far_y) - (right_x * far_x);
	float far_top_left_y = cam_y + (-out_y * far_z) + (up_y * far_y) - (right_y * far_x);
	float far_top_left_z = cam_z + (-out_z * far_z) + (up_z * far_y) - (right_z * far_x);

	float far_top_right_x = cam_x + (-out_x * far_z) + (up_x * far_y) + (right_x * far_x);
	float far_top_right_y = cam_y + (-out_y * far_z) + (up_y * far_y) + (right_y * far_x);
	float far_top_right_z = cam_z + (-out_z * far_z) + (up_z * far_y) + (right_z * far_x);

	float far_bottom_left_x = cam_x + (-out_x * far_z) + (-up_x * far_y) - (right_x * far_x);
	float far_bottom_left_y = cam_y + (-out_y * far_z) + (-up_y * far_y) - (right_y * far_x);
	float far_bottom_left_z = cam_z + (-out_z * far_z) + (-up_z * far_y) - (right_z * far_x);

	float far_bottom_right_x = cam_x + (-out_x * far_z) + (-up_x * far_y) + (right_x * far_x);
	float far_bottom_right_y = cam_y + (-out_y * far_z) + (-up_y * far_y) + (right_y * far_x);
	float far_bottom_right_z = cam_z + (-out_z * far_z) + (-up_z * far_y) + (right_z * far_x);

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_left_x, .y = near_top_left_y, .z = near_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_right_x, .y = near_top_right_y, .z = near_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_right_x, .y = near_top_right_y, .z = near_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_right_x, .y = near_bottom_right_y, .z = near_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_right_x, .y = near_bottom_right_y, .z = near_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_left_x, .y = near_bottom_left_y, .z = near_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_left_x, .y = near_bottom_left_y, .z = near_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_left_x, .y = near_top_left_y, .z = near_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_left_x, .y = near_top_left_y, .z = near_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_left_x, .y = far_top_left_y, .z = far_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_top_right_x, .y = near_top_right_y, .z = near_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_right_x, .y = far_top_right_y, .z = far_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_right_x, .y = near_bottom_right_y, .z = near_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_right_x, .y = far_bottom_right_y, .z = far_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = near_bottom_left_x, .y = near_bottom_left_y, .z = near_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_left_x, .y = far_bottom_left_y, .z = far_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	// ================= Far plane =================

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_left_x, .y = far_top_left_y, .z = far_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_right_x, .y = far_top_right_y, .z = far_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_right_x, .y = far_top_right_y, .z = far_top_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_right_x, .y = far_bottom_right_y, .z = far_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_right_x, .y = far_bottom_right_y, .z = far_bottom_right_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_left_x, .y = far_bottom_left_y, .z = far_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_bottom_left_x, .y = far_bottom_left_y, .z = far_bottom_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = far_top_left_x, .y = far_top_left_y, .z = far_top_left_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

}

void Viewer::update_driver_channels(float dt)
{
	
	for (const auto& driver : rtg.scene.drivers) {
		auto time_interval = find_time_interval(driver.times, time);
		if (time_interval.first == time_interval.second || time_interval.second == driver.times.size()) {
			if (driver.channel == S72::Driver::Channel::translation) {
				driver_channel_values[driver.node.name].translation =
				{
						.x = driver.values[time_interval.first * 3],
						.y = driver.values[time_interval.first * 3 + 1],
						.z = driver.values[time_interval.first * 3 + 2]
				};
				driver_channel_values[driver.node.name].type |= DriverChannelType::Translation;
			}
			else if (driver.channel == S72::Driver::Channel::scale) {
				driver_channel_values[driver.node.name].scale =
				{
					   .x = driver.values[time_interval.first * 3],
					   .y = driver.values[time_interval.first * 3 + 1],
					   .z = driver.values[time_interval.first * 3 + 2]
				};
				driver_channel_values[driver.node.name].type |= DriverChannelType::Scale;
			}
			else if (driver.channel == S72::Driver::Channel::rotation) {
				driver_channel_values[driver.node.name].rotation =
				{
					   .x = driver.values[time_interval.first * 4],
					   .y = driver.values[time_interval.first * 4 + 1],
					   .z = driver.values[time_interval.first * 4 + 2],
					   .w = driver.values[time_interval.first * 4 + 3]
				};
				driver_channel_values[driver.node.name].type |= DriverChannelType::Rotation;
			}
			else {
				assert(false && "unhandled driver channel");
			}
		}
		else {
			float start_time = driver.times[time_interval.first];
			float end_time = driver.times[time_interval.second];

			float t = (time - start_time) / (end_time - start_time);
			if (driver.channel == S72::Driver::Channel::translation) {
				vec4 start_value{
					driver.values[time_interval.first * 3],
					driver.values[time_interval.first * 3 + 1],
					driver.values[time_interval.first * 3 + 2],
					1.0f
				};
				vec4 end_value{
					driver.values[time_interval.second * 3],
					driver.values[time_interval.second * 3 + 1],
					driver.values[time_interval.second * 3 + 2],
					1.0f
				};
				vec4 value = interpolate(start_value, end_value, t, driver.interpolation);
				driver_channel_values[driver.node.name].translation =
				{ value[0], value[1], value[2] };
				driver_channel_values[driver.node.name].type |= DriverChannelType::Translation;
			}
			else if (driver.channel == S72::Driver::Channel::scale) {
				vec4 start_value{
					driver.values[time_interval.first * 3],
					driver.values[time_interval.first * 3 + 1],
					driver.values[time_interval.first * 3 + 2],
					1.0f
				};
				vec4 end_value{
					driver.values[time_interval.second * 3],
					driver.values[time_interval.second * 3 + 1],
					driver.values[time_interval.second * 3 + 2],
					1.0f
				};
				vec4 value = interpolate(start_value, end_value, t, driver.interpolation);
				driver_channel_values[driver.node.name].scale =
				{ value[0], value[1], value[2] };
				driver_channel_values[driver.node.name].type |= DriverChannelType::Scale;
			}
			else if (driver.channel == S72::Driver::Channel::rotation) {
				vec4 start_value{
					driver.values[time_interval.first * 4],
					driver.values[time_interval.first * 4 + 1],
					driver.values[time_interval.first * 4 + 2],
					driver.values[time_interval.first * 4 + 3]
				};
				vec4 end_value{
					driver.values[time_interval.second * 4],
					driver.values[time_interval.second * 4 + 1],
					driver.values[time_interval.second * 4 + 2],
					driver.values[time_interval.second * 4 + 3]
				};
				vec4 value = interpolate(start_value, end_value, t, driver.interpolation);
				driver_channel_values[driver.node.name].rotation =
				{ value[0], value[1], value[2], value[3] };
				driver_channel_values[driver.node.name].type |= DriverChannelType::Rotation;
			}
			else {
				assert(false && "unhandled driver channel");
			}
		}

	}
}

std::pair<uint32_t, uint32_t> Viewer::find_time_interval(const std::vector<float>& times, float t)
{
	uint32_t end_index = static_cast<uint32_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin());
	if (end_index == 0) {
		return { 0, 0 };
	}
	else {
		return { end_index - 1, end_index };
	}
}

vec4 Viewer::interpolate(const vec4& start, const vec4& end, float t, S72::Driver::Interpolation interpolation)
{
	if (interpolation == S72::Driver::Interpolation::LINEAR) {
		return start + (end - start) * t;
	}
	else if (interpolation == S72::Driver::Interpolation::STEP) {
		return start;
	}
	else if (interpolation == S72::Driver::Interpolation::SLERP) {
		float dot_value = dot(start, end);
		vec4 start_mod = start;
		if (dot_value < 0.0f) {
			start_mod = -start;
			dot_value = -dot_value;
		}

		dot_value = std::clamp(dot_value, -1.0f, 1.0f);
		const float epsilon = 1e-5f;
		if (dot_value > 1.0f - epsilon) {
			vec4 result = start + (end - start) * t;
			return result;
		}

		float theta = std::acosf(dot_value);
		float st = sin(theta);
		float s_0 = std::sinf((1.0f - t) * theta) / st;
		float s_1 = std::sinf(t * theta) / st;
		return start_mod * s_0 + end * s_1;
	}
	else {
		assert(false && "unhandled interpolation type");
		return vec4{ 0,0,0,0 };
	}
}

S72::color Viewer::srgb_to_linear(const S72::color& c)
{
	auto srgb_to_linear_channel = [](float channel) {
		//return pow(channel, 2.2f);
		return channel;
		/*if (channel <= 0.04045f) {
			return channel / 12.92f;
		}
		else {
			return std::pow((channel + 0.055f) / 1.055f, 2.4f);
		}*/
		};

	return S72::color{
		srgb_to_linear_channel(c.r),
		srgb_to_linear_channel(c.g),
		srgb_to_linear_channel(c.b),
	};
}

double Viewer::get_query_results(uint32_t workspace_index)
{
	uint64_t timestamps[2];
	auto result = vkGetQueryPoolResults(
		rtg.device, 
		query_pool, 
		workspace_index * 2,
		2,
		sizeof(timestamps), 
		timestamps,
		sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
	);

	assert(result != VK_NOT_READY);
	
	double time_ms = (timestamps[1] - timestamps[0]) * timestamp_period / 1e6;
	return time_ms;
	
	
}

void Viewer::rgbe_to_e5b9g9r9(unsigned char *rgbe)
{
	if (rgbe[0] == 0 && rgbe[1] == 0 && rgbe[2] == 0 && rgbe[3] == 0) {
		return;
	}
		
	uint32_t e = rgbe[3];
	uint32_t b = rgbe[2];
	uint32_t g = rgbe[1];
	uint32_t r = rgbe[0];
		
	float scale = std::ldexp(1.0f, e - 128 - 8);
	float fr = r * scale;
	float fg = g * scale;
	float fb = b * scale;

	// Find max for shared exponent
	float max_c = std::max(fr, std::max(fg, fb));

	int shared_exp;
	std::frexp(max_c, &shared_exp);

	// Clamp exponent to E5 range (Bias 15)
	int biased_exp = std::max(0, std::min(31, shared_exp + 15));

	// Calculate 9-bit mantissas
	float denom = std::ldexp(1.0f, biased_exp - 15 - 9);
	uint32_t r9 = (uint32_t)std::min(511.0f, std::round(fr / denom));
	uint32_t g9 = (uint32_t)std::min(511.0f, std::round(fg / denom));
	uint32_t b9 = (uint32_t)std::min(511.0f, std::round(fb / denom));

	uint32_t packed =
		(biased_exp << 27) |
		(b9 << 18) |
		(g9 << 9) |
		r9;
	memcpy(rgbe, &packed, sizeof(uint32_t));
	/*rgbe[0] = packed & 0xff;
	rgbe[1] = (packed >> 8) & 0xff;
	rgbe[2] = (packed >> 16) & 0xff;
	rgbe[3] = (packed >> 24) & 0xff; */
}
vec4 Viewer::get_current_camera_position()
{
	if (camera_mode == CameraMode::Free) {
		float ca = std::cos(free_camera.azimuth);
		float sa = std::sin(free_camera.azimuth);
		float ce = std::cos(free_camera.elevation);
		float se = std::sin(free_camera.elevation);
		
		float out_x = ce * ca;
		float out_y = ce * sa;
		float out_z = se;
		
		float eye_x = free_camera.target_x + free_camera.radius * out_x;
		float eye_y = free_camera.target_y + free_camera.radius * out_y;
		float eye_z = free_camera.target_z + free_camera.radius * out_z;
		return vec4{ eye_x, eye_y, eye_z, 0.0f };
	}
	else if (camera_mode == CameraMode::Debug) {
		float ca = std::cos(debug_camera.azimuth);
		float sa = std::sin(debug_camera.azimuth);
		float ce = std::cos(debug_camera.elevation);
		float se = std::sin(debug_camera.elevation);

		float out_x = ce * ca;
		float out_y = ce * sa;
		float out_z = se;

		float eye_x = debug_camera.target_x + debug_camera.radius * out_x;
		float eye_y = debug_camera.target_y + debug_camera.radius * out_y;
		float eye_z = debug_camera.target_z + debug_camera.radius * out_z;
		return vec4{ eye_x, eye_y, eye_z, 0.0f };
	}
	else if (camera_mode == CameraMode::Scene) {
		return vec4{ scene_camera.eye_x, scene_camera.eye_y, scene_camera.eye_z, 0.0f };
	}
	else {
		throw std::runtime_error("Not supported camera mode.");
		
	}
	return vec4{ };
}
void Viewer::render(RTG& rtg_, RTG::RenderParams const& render_params) {

	static std::unique_ptr<Timer> timer;
	timer.reset(new Timer([](double d) { std::cout << "REPORT frame-time " << d * 1000.0 << "ms" << std::endl; }));
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace& workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	//record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	//refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);
	if (rtg.configuration.profile) {
		if (workspace.ready_for_query) {
			double gpu_time = get_query_results(render_params.workspace_index);
			std::cout << "gpu time = " << gpu_time << "ms" << std::endl;
		}
		else {
			workspace.ready_for_query = true;
		}		
	}
	
	// reset the command buffer
	VK(vkResetCommandBuffer(workspace.command_buffer, 0));

	//record commands
	{
		VkCommandBufferBeginInfo commandBufferBeginInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &commandBufferBeginInfo));
	}

	// if profiling reset query
	if (rtg.configuration.profile) {
		vkCmdResetQueryPool(workspace.command_buffer, query_pool, render_params.workspace_index * 2, 2);
	}

	if (!lines_vertices.empty()) {
		// realloc buffers if needed
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.line_vertices_src.handle == VK_NULL_HANDLE || workspace.line_vertices_src.size < needed_bytes) {
			size_t new_bytes = (needed_bytes + 4096) / 4096 * 4096;

			if (workspace.line_vertices_src.handle != VK_NULL_HANDLE) {
				rtg.helpers.destroy_buffer(std::move(workspace.line_vertices_src));
			}
			if (workspace.line_vertices.handle != VK_NULL_HANDLE) {
				rtg.helpers.destroy_buffer(std::move(workspace.line_vertices));
			}

			workspace.line_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.line_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			std::cout << "Re-allocated lines buffers to " << new_bytes << std::endl;

		}

		assert(workspace.line_vertices_src.size == workspace.line_vertices.size);
		assert(workspace.line_vertices_src.size >= needed_bytes);

		assert(workspace.line_vertices_src.allocation.mapped);
		std::memcpy(workspace.line_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);

		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.line_vertices_src.handle, workspace.line_vertices.handle, 1, &copy_region);
		{
			ObjectsPipeline::Camera camera{
				.CLIP_FROM_WORLD = CLIP_FROM_WORLD,
				.EYE = get_current_camera_position()
			};
			assert(workspace.Camera_src.size == sizeof(camera));

			memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));
			assert(workspace.Camera_src.size == workspace.Camera.size);

			VkBufferCopy camera_copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = workspace.Camera_src.size
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &camera_copy_region);
		}
		{
			VkMemoryBarrier memory_barrier{
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT
			};

			vkCmdPipelineBarrier(
				workspace.command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0,
				1, &memory_barrier,//memoryBarriers (count, data)
				0, nullptr,//bufferBarriers (count, data)
				0, nullptr//imageBarriers (count, data)
			);
		}


	}

	{
		assert(workspace.World_src.size == sizeof(world));
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	if (!object_instances.empty()) {
		// realloc buffers if needed
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
		if (workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes) {
			size_t new_bytes = (needed_bytes + 4096) / 4096 * 4096;

			if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}
			if (workspace.Transforms.handle != VK_NULL_HANDLE) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}


			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			// Update Descriptor Set
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size
			};

			std::array<VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info
				}
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()),
				writes.data(),
				0, nullptr
			);
			std::cout << "Re-allocated object Transforms buffers to " << new_bytes << std::endl;

		}

		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);

		{
			assert(workspace.Transforms_src.allocation.mapped);
			ObjectsPipeline::Transform* out = reinterpret_cast<ObjectsPipeline::Transform*>(workspace.Transforms_src.allocation.data());
			for (ObjectInstance& inst : object_instances) {
				inst.transform.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * inst.transform.WORLD_FROM_LOCAL;
				*out = inst.transform;
				++out;
			}
		}
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);


		{
			ObjectsPipeline::Camera camera{
				.CLIP_FROM_WORLD = CLIP_FROM_WORLD,
				.EYE = get_current_camera_position()
			};
			assert(workspace.Camera_src.size == sizeof(camera));

			memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));
			assert(workspace.Camera_src.size == workspace.Camera.size);

			VkBufferCopy camera_copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = workspace.Camera_src.size
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &camera_copy_region);
		}
		{
			VkMemoryBarrier memory_barrier{
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT
			};

			vkCmdPipelineBarrier(
				workspace.command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0,
				1, &memory_barrier,//memoryBarriers (count, data)
				0, nullptr,//bufferBarriers (count, data)
				0, nullptr//imageBarriers (count, data)
			);
		}


	}

	

	//render pass
	{
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color = {.float32{0.0f, 0.0f, 0.0f, 1.0f}} },
			VkClearValue{.depthStencil = {.depth = 1.0f, .stencil = 0}}
		};

		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea = {
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent
			},
			.clearValueCount = static_cast<uint32_t>(clear_values.size()),
			.pClearValues = clear_values.data()
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
		{
			VkRect2D scissor{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent
			};

			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);

			if (camera_mode == CameraMode::Scene) {
				float x_scale = 1.0f;
				float y_scale = 1.0f;

				float image_aspect = rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height;

				if (image_aspect > scene_camera.aspect) {
					x_scale = scene_camera.aspect / image_aspect;
				}
				else {
					y_scale = image_aspect / scene_camera.aspect;
				}

				VkViewport viewport{
					.x = (1 - x_scale) * 0.5f * float(rtg.swapchain_extent.width),
					.y = (1 - y_scale) * 0.5f * float(rtg.swapchain_extent.height),
					.width = float(rtg.swapchain_extent.width) * x_scale,
					.height = float(rtg.swapchain_extent.height) * y_scale,
					.minDepth = 0.0f,
					.maxDepth = 1.0f
				};
				vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
			}
			else {

				VkViewport viewport{
					.x = 0,
					.y = 0,
					.width = float(rtg.swapchain_extent.width),
					.height = float(rtg.swapchain_extent.height),
					.minDepth = 0.0f,
					.maxDepth = 1.0f
				};
				vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
			}


		}

		{
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			{
				//push constants:
				BackgroundPipeline::Push push{
					.time = time
				};

				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BackgroundPipeline::Push), &push);
			}
			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		if (!lines_vertices.empty())
		{
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);
			{
				std::array<VkBuffer, 1> vertex_buffers{ workspace.line_vertices.handle };
				std::array<VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
				//vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);

			}

			{
				//bind descriptor set layout
				std::array<VkDescriptorSet, 1> descriptor_sets{ workspace.Camera_descriptors };
				vkCmdBindDescriptorSets(
					workspace.command_buffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					lines_pipeline.layout,
					0,
					static_cast<uint32_t>(descriptor_sets.size()),
					descriptor_sets.data(),
					0,
					nullptr);
			}
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		//objects
		{
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);
			{
				ObjectsPipeline::Push push{
					.exposure = rtg.configuration.exposure,
					.tone_operator = static_cast<int>(rtg.configuration.tone_operator)
				};

				vkCmdPushConstants(workspace.command_buffer, objects_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectsPipeline::Push), &push);
			}
			{
				std::array<VkBuffer, 1> vertex_buffers{ mesh_vertex_buffer.handle };
				std::array<VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, static_cast<uint32_t>(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

				//Camera descriptor set is still bound(!)

			}

			if (rtg.configuration.indexed) {
				vkCmdBindIndexBuffer(
					workspace.command_buffer,
					mesh_indices_buffer.handle,
					0,
					VK_INDEX_TYPE_UINT32
				);
			}

			{
				std::array<VkDescriptorSet, 2> descriptor_sets{
					workspace.World_descriptors,
					workspace.Transforms_descriptors									
				};

				vkCmdBindDescriptorSets(
					workspace.command_buffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					objects_pipeline.layout,
					0,
					uint32_t(descriptor_sets.size()),
					descriptor_sets.data(),
					0,
					nullptr
				);

			}
			if (rtg.configuration.profile) {
				vkCmdWriteTimestamp(workspace.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, render_params.workspace_index * 2);
			}

			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);
				if (inst.texture_type == ObjectInstance::Type::ALBEDO) {
					if (!rtg.scene.environments.empty()) {
						continue;
					}
					std::array<VkDescriptorSet, 4> descriptor_sets{  texture_descriptors[inst.texture], workspace.Camera_descriptors, texture_descriptors[env_texture_index], texture_descriptors[inst.normal_map]};

					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						2,
						static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data(),
						0, nullptr
					);

					if (!rtg.configuration.indexed) {
						vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
					}
					else {
						vkCmdDrawIndexed(
							workspace.command_buffer,
							inst.vertices.count,
							1,
							inst.vertices.first,
							0,
							index
						);
					}
				}								
			}

			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.env_handle);

			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);
				if (inst.texture_type == ObjectInstance::Type::ENV) {
					std::array<VkDescriptorSet, 4> descriptor_sets{ texture_descriptors[inst.texture], workspace.Camera_descriptors, texture_descriptors[env_texture_index], texture_descriptors[inst.normal_map] };

					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						2,
						static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data(),
						0, nullptr
					);

					if (!rtg.configuration.indexed) {
						vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
					}
					else {
						vkCmdDrawIndexed(
							workspace.command_buffer,
							inst.vertices.count,
							1,
							inst.vertices.first,
							0,
							index
						);
					}
				}
			}

			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.mirror_handle);

			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);
				if (inst.texture_type == ObjectInstance::Type::MIRROR) {
					std::array<VkDescriptorSet, 4> descriptor_sets{ texture_descriptors[inst.texture], workspace.Camera_descriptors, texture_descriptors[env_texture_index], texture_descriptors[inst.normal_map] };

					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						2,
						static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data(),
						0, nullptr
					);

					if (!rtg.configuration.indexed) {
						vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
					}
					else {
						vkCmdDrawIndexed(
							workspace.command_buffer,
							inst.vertices.count,
							1,
							inst.vertices.first,
							0,
							index
						);
					}
				}
			}

			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.lambertian_env_handle);

			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);
				if (inst.texture_type == ObjectInstance::Type::ALBEDO) {
					if (rtg.scene.environments.empty()) {
						continue;
					}
					std::array<VkDescriptorSet, 4> descriptor_sets{ texture_descriptors[inst.texture], workspace.Camera_descriptors, texture_descriptors[env_texture_index], texture_descriptors[inst.normal_map] };

					vkCmdBindDescriptorSets(
						workspace.command_buffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						objects_pipeline.layout,
						2,
						static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data(),
						0, nullptr
					);

					if (!rtg.configuration.indexed) {
						vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
					}
					else {
						vkCmdDrawIndexed(
							workspace.command_buffer,
							inst.vertices.count,
							1,
							inst.vertices.first,
							0,
							index
						);
					}
				}
			}


			if (rtg.configuration.profile) {
				vkCmdWriteTimestamp(workspace.command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, render_params.workspace_index * 2 + 1);
			}
			//vkCmdDraw(workspace.command_buffer, static_cast<uint32_t>(object_vertices.size / sizeof(PosColVertex)), 1, 0, 0);

		}


		vkCmdEndRenderPass(workspace.command_buffer);
	}

	


	VK(vkEndCommandBuffer(workspace.command_buffer));
	//submit `workspace.command buffer` for the GPU to run:
	//refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
	{
		std::array<VkSemaphore, 1> wait_semaphores{
			render_params.image_available
		};
		std::array<VkPipelineStageFlags, 1> wait_stages{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};

		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array<VkSemaphore, 1> signal_semaphores{
			render_params.image_done
		};

		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available));
	}


}


void Viewer::update(float dt) {
	time += dt;

	lines_vertices.clear();

	update_driver_channels(dt);

	{
		Timer timer([](double time) { std::cout << "Time spent on scene graph traversal: " << time * 1000 << "ms" << std::endl; });
		load_objects();
	}

	if (camera_mode == CameraMode::Scene)
	{
		CLIP_FROM_WORLD = perspective(
			scene_camera.fov,
			scene_camera.aspect,
			scene_camera.near,
			scene_camera.far
		) *
			scene_camera.inverse;
		/*look_at(
			scene_camera.eye_x, scene_camera.eye_y, scene_camera.eye_z,
			scene_camera.eye_x + scene_camera.forward_x, scene_camera.eye_y + scene_camera.forward_y, scene_camera.eye_z + scene_camera.forward_z,
			scene_camera.up_x, scene_camera.up_y, scene_camera.up_z
		);*/
	}
	else if (camera_mode == CameraMode::Free) {
		CLIP_FROM_WORLD = perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			free_camera.near,
			free_camera.far
		) * orbit(
			free_camera.target_x, free_camera.target_y, free_camera.target_z,
			free_camera.azimuth, free_camera.elevation, free_camera.radius
		);
	}
	else if (camera_mode == CameraMode::Debug) {
		CLIP_FROM_WORLD = perspective(
			debug_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			debug_camera.near,
			debug_camera.far
		) * orbit(
			debug_camera.target_x, debug_camera.target_y, debug_camera.target_z,
			debug_camera.azimuth, debug_camera.elevation, debug_camera.radius
		);
		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM)
		{
			draw_frustum();
		}
	}
	else {
		assert(false && "invalid camera mode");
	}
}


void Viewer::on_input(InputEvent const& evt) {
	if (action) {
		action(evt);
		return;
	}

	// general
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		if (camera_mode != CameraMode::Debug) {
			previous_camera_mode = camera_mode;
			camera_mode = CameraMode((int(camera_mode) + 1) % 2);
		}		
		return;
	}

	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_D) {
		if (camera_mode == CameraMode::Debug) {
			camera_mode = previous_camera_mode;
			previous_camera_mode = CameraMode::Debug;
		}
		else {
			previous_camera_mode = camera_mode;
			camera_mode = CameraMode::Debug;
		}				
		return;
	}

	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_R) {
		time = 0;
	}


	if (camera_mode == CameraMode::Scene) {
		return;
	}

	// free camera control
	OrbitCamera& camera = (camera_mode == CameraMode::Free ? free_camera : debug_camera);

	if (evt.type == InputEvent::MouseWheel) {
		camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
		camera.radius = std::min(camera.radius, 2.0f * camera.far);
		camera.radius = std::max(camera.radius, 0.5f * camera.near);
		return;
	}

	if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
		//start panning
		float init_x = evt.button.x;
		float init_y = evt.button.y;
		OrbitCamera init_camera = camera;

		action = [this, &camera, init_x, init_y, init_camera](InputEvent const& evt) {
			if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
				//cancel upon button lifted:
				action = nullptr;
				return;
			}
			if (evt.type == InputEvent::MouseMotion) {
				// Image height at the plane of the target point
				float height = 2.0f * std::tan(camera.fov * 0.5f) * camera.radius;

				// motion at target point
				float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
				float dy = (evt.motion.y - init_y) / rtg.swapchain_extent.height * height;

				//compute camera transform to extract right (first row) and up (second row):
				mat4 camera_from_world = orbit(
					init_camera.target_x, init_camera.target_y, init_camera.target_z,
					init_camera.azimuth, init_camera.elevation, init_camera.radius
				);

				//move the desired distance:
				camera.target_x = init_camera.target_x - dx * camera_from_world[0] - dy * camera_from_world[1];
				camera.target_y = init_camera.target_y - dx * camera_from_world[4] - dy * camera_from_world[5];
				camera.target_z = init_camera.target_z - dx * camera_from_world[8] - dy * camera_from_world[9];

				return;
			}
			};

		return;
	}

	if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
		//start tumbling
		// std::cout << "Start tumbling" << std::endl;
		float init_x = evt.button.x;
		float init_y = evt.button.y;
		OrbitCamera init_camera = camera;

		action = [this, &camera, init_x, init_y, init_camera](InputEvent const& evt) {
			if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
				action = nullptr;
				//std::cout << "Tumbling ended." << std::endl;
				return;
			}
			if (evt.type == InputEvent::MouseMotion) {
				float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.width;
				float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height;

				float speed = float(M_PI);
				float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f);

				camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
				camera.elevation = init_camera.elevation - dy * speed;

				const float twopi = 2.0f * float(M_PI);
				camera.azimuth -= std::round(camera.azimuth / twopi) * twopi;
				camera.elevation -= std::round(camera.elevation / twopi) * twopi;
				return;
			}
			};
		return;
	}

}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> Viewer::load_mesh_vertices_indexed() {	
	std::vector<Vertex> vertices;
	// assume no indices data in s72
	std::vector<uint32_t> indices;
	std::unordered_map<Vertex, uint32_t> vertex_to_index;
	uint32_t current_index = 0;

	for (const auto& [name, mesh] : rtg.scene.meshes) {
		
		vertex_to_index.clear();
		uint32_t count = mesh.count;
		uint32_t vertex_first = static_cast<uint32_t>(vertices.size());
		uint32_t index_first = static_cast<uint32_t>(indices.size());


		//vertices.resize(vertices.size() + count);

		uint32_t position_offset = mesh.attributes.at("POSITION").offset;
		uint32_t normal_offset = mesh.attributes.at("NORMAL").offset;
		uint32_t tangent_offset = mesh.attributes.at("TANGENT").offset;
		uint32_t texcoord_offset = mesh.attributes.at("TEXCOORD").offset;
		uint32_t stride = mesh.attributes.at("POSITION").stride;
		const std::vector<char>& content = mesh.attributes.at("POSITION").src.content;

		assert(stride == mesh.attributes.at("NORMAL").stride);
		assert(stride == mesh.attributes.at("TANGENT").stride);
		assert(stride == mesh.attributes.at("TEXCOORD").stride);

		for (uint32_t i = 0; i < count; i++) {
			uint32_t strides = i * stride;
			Vertex v;

			v.Position.x = *reinterpret_cast<const float*>(content.data() + position_offset + strides);
			v.Position.y = *reinterpret_cast<const float*>(content.data() + position_offset + strides + sizeof(float));
			v.Position.z = *reinterpret_cast<const float*>(content.data() + position_offset + strides + 2 * sizeof(float));

			v.Normal.x = *reinterpret_cast<const float*>(content.data() + normal_offset + strides);
			v.Normal.y = *reinterpret_cast<const float*>(content.data() + normal_offset + strides + sizeof(float));
			v.Normal.z = *reinterpret_cast<const float*>(content.data() + normal_offset + strides + 2 * sizeof(float));

			v.Tangent.x = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides);
			v.Tangent.y = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + sizeof(float));
			v.Tangent.z = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + 2 * sizeof(float));
			v.Tangent.w = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + 3 * sizeof(float));

			v.TexCoord.s = *reinterpret_cast<const float*>(content.data() + texcoord_offset + strides);
			v.TexCoord.t = *reinterpret_cast<const float*>(content.data() + texcoord_offset + strides + sizeof(float));

			if (vertex_to_index.count(v)) {
				indices.push_back(vertex_to_index[v]);
			}
			else {
				vertex_to_index[v] = current_index;
				indices.push_back(current_index);
				vertices.push_back(v);
				current_index++;
			}
		}

		mesh_vertices[name] = MeshSlice{
			.first = vertex_first,
			.count = static_cast<uint32_t>(vertices.size()) - vertex_first
		};

		mesh_indices[name] = MeshSlice{
			.first = index_first,
			.count = static_cast<uint32_t>(indices.size()) - index_first
		};
	}

	return { vertices, indices };
}

std::vector<Vertex> Viewer::load_mesh_vertices() {
	std::vector<Vertex> vertices;
	
	for (const auto& [name, mesh] : rtg.scene.meshes) {
		uint32_t count = mesh.count;
		uint32_t first = static_cast<uint32_t>(vertices.size());

		vertices.resize(vertices.size() + count);

		uint32_t position_offset = mesh.attributes.at("POSITION").offset;
		uint32_t normal_offset = mesh.attributes.at("NORMAL").offset;
		uint32_t tangent_offset = mesh.attributes.at("TANGENT").offset;
		uint32_t texcoord_offset = mesh.attributes.at("TEXCOORD").offset;
		uint32_t stride = mesh.attributes.at("POSITION").stride;
		const std::vector<char>& content = mesh.attributes.at("POSITION").src.content;

		assert(stride == mesh.attributes.at("NORMAL").stride);
		assert(stride == mesh.attributes.at("TANGENT").stride);
		assert(stride == mesh.attributes.at("TEXCOORD").stride);

		for (uint32_t i = 0; i < count; i++) {
			uint32_t strides = i * stride;
			

			// position
			vertices[first + i].Position.x = *reinterpret_cast<const float*>(content.data() + position_offset + strides);
			vertices[first + i].Position.y = *reinterpret_cast<const float*>(content.data() + position_offset + strides + sizeof(float));
			vertices[first + i].Position.z = *reinterpret_cast<const float*>(content.data() + position_offset + strides + 2 * sizeof(float));

			// normal
			vertices[first + i].Normal.x = *reinterpret_cast<const float*>(content.data() + normal_offset + strides);
			vertices[first + i].Normal.y = *reinterpret_cast<const float*>(content.data() + normal_offset + strides + sizeof(float));
			vertices[first + i].Normal.z = *reinterpret_cast<const float*>(content.data() + normal_offset + strides + 2 * sizeof(float));

			// tangent
			vertices[first + i].Tangent.x = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides);
			vertices[first + i].Tangent.y = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + sizeof(float));
			vertices[first + i].Tangent.z = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + 2 * sizeof(float));
			vertices[first + i].Tangent.w = *reinterpret_cast<const float*>(content.data() + tangent_offset + strides + 3 * sizeof(float));

			// texcoord
			vertices[first + i].TexCoord.s = *reinterpret_cast<const float*>(content.data() + texcoord_offset + strides);
			vertices[first + i].TexCoord.t = *reinterpret_cast<const float*>(content.data() + texcoord_offset + strides + sizeof(float));

		}

		mesh_vertices[name] = MeshSlice{
			.first = first,
			.count = count
		};
	}

	return vertices;
}

void Viewer::load_objects() {
	object_instances.clear();
	scene_camera.ready = false;
	delayed_culling_objects.clear();
	for (const auto root : rtg.scene.scene.roots) {

		const mat4 identity = mat4{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};


		load_objects(root, identity, identity);
	}

	for (auto& [name, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL] : delayed_culling_objects) {
		// must be scene camera
		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {
			if (is_mesh_in_frustum(name, mesh_bounding_boxes[name], WORLD_FROM_LOCAL)) {
				render_mesh(rtg.scene.meshes[name], WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
			}
		}
		else {
			render_mesh(rtg.scene.meshes[name], WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
		}

	}
}

void Viewer::load_objects(const S72::Node* node_root, const mat4& node_world_from_local, const mat4& node_world_from_local_normal) {
	// compute parent from local	
	std::stack<std::tuple<const S72::Node*, mat4, mat4>> stack;

	stack.push(std::make_tuple( static_cast<const S72::Node*>(node_root), node_world_from_local, node_world_from_local_normal ));
	
	while (!stack.empty()) {
		auto [root, world_from_local, world_from_local_normal] = stack.top();
		float sx = root->scale.x;
		float sy = root->scale.y;
		float sz = root->scale.z;

		float rx = root->rotation.x;
		float ry = root->rotation.y;
		float rz = root->rotation.z;
		float rw = root->rotation.w;

		float tx = root->translation.x;
		float ty = root->translation.y;
		float tz = root->translation.z;

		if (driver_channel_values.count(root->name)) {
			auto& channel = driver_channel_values[root->name];

			if (channel.type & DriverChannelType::Translation) {
				tx = channel.translation.x;
				ty = channel.translation.y;
				tz = channel.translation.z;
			}
			if (channel.type & DriverChannelType::Rotation) {

				rx = channel.rotation.x;
				ry = channel.rotation.y;
				rz = channel.rotation.z;
				rw = channel.rotation.w;
			}
			if (channel.type & DriverChannelType::Scale) {

				sx = channel.scale.x;
				sy = channel.scale.y;
				sz = channel.scale.z;
			}

		}


		const mat4 parent_from_local = mat4{
			(1 - 2 * (ry * ry + rz * rz)) * sx,	2 * (rx * ry + rw * rz) * sx,	2 * (rx * rz - rw * ry) * sx,	0.0f,
			2 * (rx * ry - rw * rz) * sy,	(1 - 2 * (rx * rx + rz * rz)) * sy,	2 * (ry * rz + rw * rx) * sy,	0.0f,
			2 * (rx * rz + rw * ry) * sz,	2 * (ry * rz - rw * rx) * sz,	(1 - 2 * (rx * rx + ry * ry)) * sz,	0.0f,
			tx,	ty,	tz,	1.0f
		};


		const mat4 parent_from_local_normal = mat4{
				(1 - 2 * (ry * ry + rz * rz)) / sx, 2 * (rx * ry + rw * rz) / sx, 2 * (rx * rz - rw * ry) / sx, 0.0f,
				2 * (rx * ry - rw * rz) / sy, (1 - 2 * (rx * rx + rz * rz)) / sy, 2 * (ry * rz + rw * rx) / sy, 0.0f,
				2 * (rx * rz + rw * ry) / sz, 2 * (ry * rz - rw * rx) / sz, (1 - 2 * (rx * rx + ry * ry)) / sz, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
		};


		const mat4 WORLD_FROM_LOCAL = world_from_local * parent_from_local;
		const mat4 WORLD_FROM_LOCAL_NORMAL = world_from_local_normal * parent_from_local_normal;

		if ((camera_mode == CameraMode::Scene || previous_camera_mode == CameraMode::Scene) && root->camera != nullptr) {
			if (rtg.configuration.camera_name != "" && root->camera->name != rtg.configuration.camera_name) {
				// skip this camera, not the one we want
				return;
			}
			scene_camera.ready = true;
			//assert(root->camera->name == rtg.configuration.camera_name);
			if (std::holds_alternative<S72::Camera::Perspective>(root->camera->projection)) {
				S72::Camera::Perspective perspective = std::get<S72::Camera::Perspective>(root->camera->projection);
				scene_camera.fov = perspective.vfov;
				scene_camera.near = perspective.near;
				scene_camera.far = perspective.far;
				scene_camera.aspect = perspective.aspect;
			}

			scene_camera.inverse = inverse_mat(WORLD_FROM_LOCAL);
			vec4 cam_pos = WORLD_FROM_LOCAL * vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
			scene_camera.eye_x = cam_pos[0];
			scene_camera.eye_y = cam_pos[1];
			scene_camera.eye_z = cam_pos[2];



			vec4 forward = WORLD_FROM_LOCAL_NORMAL * vec4{ 0.0f, 0.0f, -1.0f, 0.0f };
			float len = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
			forward = forward / len;
			scene_camera.forward_x = forward[0];
			scene_camera.forward_y = forward[1];
			scene_camera.forward_z = forward[2];

			vec4 up = WORLD_FROM_LOCAL_NORMAL * vec4{ 0.0f, 1.0f, 0.0f, 0.0f };
			float up_len = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);

			up = up / up_len;

			scene_camera.up_x = up[0];
			scene_camera.up_y = up[1];
			scene_camera.up_z = up[2];
		}

		/*if (root->environment != nullptr) {
			world.ENV_WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL;
		}*/

		if (root->light != nullptr) {
			if (std::holds_alternative<S72::Light::Sun>(root->light->source)) {
				S72::Light::Sun sun = std::get<S72::Light::Sun>(root->light->source);
				assert(sun.angle == 0 || std::abs(sun.angle - float(M_PI)) < 1e-4f);

				if (sun.angle == 0) {
					vec4 dir = WORLD_FROM_LOCAL * vec4{ 0.0f, 0.0f, 1.0f, 0.0f };
					float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
					dir[0] = dir[0] / len;
					dir[1] = dir[1] / len;
					dir[2] = dir[2] / len;
					world.SUN_DIRECTION.x = dir[0];
					world.SUN_DIRECTION.y = dir[1];
					world.SUN_DIRECTION.z = dir[2];

					world.SUN_ENERGY.r = sun.strength * root->light->tint.r;
					world.SUN_ENERGY.g = sun.strength * root->light->tint.g;
					world.SUN_ENERGY.b = sun.strength * root->light->tint.b;
				}
				else if (std::abs(sun.angle - float(M_PI)) < 1e-4f) {
					vec4 dir = WORLD_FROM_LOCAL * vec4{ 0.0f, 0.0f, 1.0f, 0.0f };
					float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
					dir[0] = dir[0] / len;
					dir[1] = dir[1] / len;
					dir[2] = dir[2] / len;
					world.SKY_DIRECTION.x = dir[0];
					world.SKY_DIRECTION.y = dir[1];
					world.SKY_DIRECTION.z = dir[2];
					world.SKY_ENERGY.r = sun.strength * root->light->tint.r;
					world.SKY_ENERGY.g = sun.strength * root->light->tint.g;
					world.SKY_ENERGY.b = sun.strength * root->light->tint.b;
				}
			}
			else {
				vec4 dir = WORLD_FROM_LOCAL * vec4{ 0.0f, 0.0f, 1.0f, 0.0f };
				float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
				dir[0] = dir[0] / len;
				dir[1] = dir[1] / len;
				dir[2] = dir[2] / len;
				world.SKY_DIRECTION.x = dir[0];
				world.SKY_DIRECTION.y = dir[1];
				world.SKY_DIRECTION.z = dir[2];
				world.SKY_ENERGY.r = 1;
				world.SKY_ENERGY.g = 1;
				world.SKY_ENERGY.b = 1;
				//throw std::runtime_error("Unsupported light type");
			}
		}

		if (root->mesh != nullptr) {
			if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::NONE) {
				render_mesh(*root->mesh, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
			}
			else if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {
				if (camera_mode != CameraMode::Scene) {
					if (is_mesh_in_frustum(root->mesh->name, mesh_bounding_boxes[root->mesh->name], WORLD_FROM_LOCAL)) {
						render_mesh(*root->mesh, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
					}
				}
				else if (scene_camera.ready) {
					if (is_mesh_in_frustum(root->mesh->name, mesh_bounding_boxes[root->mesh->name], WORLD_FROM_LOCAL)) {
						render_mesh(*root->mesh, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
					}
				}
				else {
					delayed_culling_objects.emplace_back(root->mesh->name, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
				}
			}

			if (camera_mode == CameraMode::Debug && rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {

				const auto& bounding_box = mesh_bounding_boxes[root->mesh->name];
				vec4 corners[] = {
					vec4{ bounding_box.min_x, bounding_box.min_y, bounding_box.min_z, 1.0f },
					vec4{ bounding_box.max_x, bounding_box.min_y, bounding_box.min_z, 1.0f },
					vec4{ bounding_box.min_x, bounding_box.max_y, bounding_box.min_z, 1.0f },
					vec4{ bounding_box.max_x, bounding_box.max_y, bounding_box.min_z, 1.0f },
					vec4{ bounding_box.min_x, bounding_box.min_y, bounding_box.max_z, 1.0f },
					vec4{ bounding_box.max_x, bounding_box.min_y, bounding_box.max_z, 1.0f },
					vec4{ bounding_box.min_x, bounding_box.max_y, bounding_box.max_z, 1.0f },
					vec4{ bounding_box.max_x, bounding_box.max_y, bounding_box.max_z, 1.0f },
				};

				for (int i = 0; i < 8; i++) {
					corners[i] = WORLD_FROM_LOCAL * corners[i];
				}

				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[0][0], .y = corners[0][1], .z = corners[0][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[1][0], .y = corners[1][1], .z = corners[1][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[0][0], .y = corners[0][1], .z = corners[0][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[2][0], .y = corners[2][1], .z = corners[2][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[0][0], .y = corners[0][1], .z = corners[0][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[4][0], .y = corners[4][1], .z = corners[4][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[7][0], .y = corners[7][1], .z = corners[7][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[6][0], .y = corners[6][1], .z = corners[6][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[5][0], .y = corners[5][1], .z = corners[5][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[7][0], .y = corners[7][1], .z = corners[7][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[7][0], .y = corners[7][1], .z = corners[7][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[3][0], .y = corners[3][1], .z = corners[3][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[4][0], .y = corners[4][1], .z = corners[4][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[5][0], .y = corners[5][1], .z = corners[5][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[4][0], .y = corners[4][1], .z = corners[4][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[6][0], .y = corners[6][1], .z = corners[6][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[2][0], .y = corners[2][1], .z = corners[2][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[6][0], .y = corners[6][1], .z = corners[6][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[2][0], .y = corners[2][1], .z = corners[2][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[3][0], .y = corners[3][1], .z = corners[3][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[1][0], .y = corners[1][1], .z = corners[1][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[5][0], .y = corners[5][1], .z = corners[5][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[1][0], .y = corners[1][1], .z = corners[1][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
				lines_vertices.emplace_back(PosColVertex{
					.Position = {.x = corners[3][0], .y = corners[3][1], .z = corners[3][2] },
					.Color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f }
					});
			}

		}

		stack.pop();

		for (const auto child : root->children) {
			stack.emplace(child, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
		}


	}
	
	// recurse to children
	
}

void Viewer::render_mesh(const S72::Mesh& mesh, const mat4& world_from_local, const mat4& world_from_local_normal)
{
	uint32_t texture_index = 0;
	uint32_t normal_map_index = 1;
	ObjectInstance::Type texture_type = ObjectInstance::Type::ALBEDO;

	if (mesh.material != nullptr) {
		// add object instance
		if (std::holds_alternative<S72::Material::Lambertian>(mesh.material->brdf)) {
			S72::Material::Lambertian lambert = std::get<S72::Material::Lambertian>(mesh.material->brdf);
			texture_type = ObjectInstance::Type::ALBEDO;
			if (std::holds_alternative<S72::color>(lambert.albedo)) {
				S72::color albedo_color = std::get<S72::color>(lambert.albedo);
				texture_index = texture_color_to_index.at(albedo_color);
			}
			else if (std::holds_alternative<S72::Texture*>(lambert.albedo)) {
				S72::Texture* albedo_texture = std::get<S72::Texture*>(lambert.albedo);
				std::string texture_key = albedo_texture->src + ", format " + std::to_string(int(albedo_texture->type)) + ", type " + std::to_string(int(albedo_texture->format));
				texture_index = texture_name_to_index.at(texture_key);
			}			
		}
		else if (std::holds_alternative<S72::Material::Environment>(mesh.material->brdf)) {
			
			texture_index = env_texture_index;
			texture_type = ObjectInstance::Type::ENV;
		}
		else if (std::holds_alternative<S72::Material::Mirror>(mesh.material->brdf)) {
			texture_index = env_texture_index;
			texture_type = ObjectInstance::Type::MIRROR;
		}
		else {
			//throw std::runtime_error("Unsupported material type");			
		}

		if (mesh.material->normal_map != nullptr) {
			std::string texture_key = mesh.material->normal_map->src + ", format " + std::to_string(int(mesh.material->normal_map->type)) + ", type " + std::to_string(int(mesh.material->normal_map->format));
			normal_map_index = texture_name_to_index.at(texture_key);
		}
	}

	if (!rtg.configuration.indexed) {
		object_instances.emplace_back(ObjectInstance{
			.vertices = mesh_vertices.at(mesh.name),
			.transform = {
				//.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
				.WORLD_FROM_LOCAL = world_from_local,
				.WORLD_FROM_LOCAL_NORMAL = world_from_local_normal
			},
			.texture = texture_index,
			.texture_type = texture_type,
			.normal_map = normal_map_index
			});
	}
	else {
		object_instances.emplace_back(ObjectInstance{
			.vertices = mesh_indices.at(mesh.name),
			.transform = {
				//.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
				.WORLD_FROM_LOCAL = world_from_local,
				.WORLD_FROM_LOCAL_NORMAL = world_from_local_normal
			},
			.texture = texture_index,
			.texture_type = texture_type,
			.normal_map = normal_map_index
			});
	}
	
}


void Viewer::load_textures() {
	textures.reserve(rtg.scene.textures.size());

	S72::color default_material_albedo = { 0.8f, 0.8f, 0.8f };
	textures.emplace_back(rtg.helpers.create_image(
		VkExtent2D{ .width = 1, .height = 1 },
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	));

	rtg.helpers.transfer_to_image(&default_material_albedo, 12, textures.back());

	S72::color default_normal_map = { 0.5f, 0.5f, 1.0f };
	textures.emplace_back(rtg.helpers.create_image(
		VkExtent2D{ .width = 1, .height = 1 },
		VK_FORMAT_R32G32B32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	));
	rtg.helpers.transfer_to_image(&default_normal_map, 12, textures.back());



	for (const auto& [name, texture] : rtg.scene.textures) {
		if (texture.type == S72::Texture::Type::cube) {
			int tex_width, tex_height, tex_channels;
			stbi_set_flip_vertically_on_load(false);
			unsigned char* image = stbi_load(texture.path.c_str(), &tex_width, &tex_height, &tex_channels, 4);
			if (image == nullptr) {
				throw std::runtime_error("Failed to load texture image: " + texture.path);
			}
			
			assert(tex_height % 6 == 0);

			for (size_t i = 0; i < tex_width * tex_height; i++) {
				size_t j = 4 * i;
				rgbe_to_e5b9g9r9(&image[j]);				
			}			

			texture_name_to_index[name] = static_cast<uint32_t>(textures.size());
			env_texture_index = static_cast<uint32_t>(textures.size());
		
			textures.emplace_back(rtg.helpers.create_cubemap(
				VkExtent2D{ .width = static_cast<uint32_t>(tex_width), .height = static_cast<uint32_t>(tex_height) / 6 },
				VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			rtg.helpers.transfer_to_cubemap(image, tex_width * tex_height * 4, textures.back());
			stbi_image_free(image);

		}
		else {
			int tex_width, tex_height, tex_channels;
			stbi_set_flip_vertically_on_load(true);
			unsigned char* image = stbi_load(texture.path.c_str(), &tex_width, &tex_height, &tex_channels, 0);
			if (image == nullptr) {
				throw std::runtime_error("Failed to load texture image: " + texture.path);
			}

			VkFormat format = VK_FORMAT_UNDEFINED;
			if (tex_channels == 1) {
				continue;
			}
			if (tex_channels == 3) {
				if (texture.format == S72::Texture::Format::linear) {
					format = VK_FORMAT_R8G8B8_UNORM;
				}
				else if (texture.format == S72::Texture::Format::srgb) {
					format = VK_FORMAT_R8G8B8_SRGB;
				}
				else {
					throw std::runtime_error("Unsupported texture format");
				}
			}
			else if (tex_channels == 3) {
				if (texture.format == S72::Texture::Format::linear) {
					format = VK_FORMAT_R8G8B8A8_UNORM;
				}
				else if (texture.format == S72::Texture::Format::srgb) {
					format = VK_FORMAT_R8G8B8A8_SRGB;
				}
				else {
					throw std::runtime_error("Unsupported texture format");
				}
			}
			else {
				throw std::runtime_error("Unsupported texture format");
			}

			texture_name_to_index[name] = static_cast<uint32_t>(textures.size());

			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = static_cast<uint32_t>(tex_width), .height = static_cast<uint32_t>(tex_height) },
				format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			rtg.helpers.transfer_to_image(image, tex_width * tex_height * tex_channels, textures.back());
			stbi_image_free(image);

		}
		
	}

	bool lambertian = false;
	for (const auto& [name, material] : rtg.scene.materials) {
		if (std::holds_alternative<S72::Material::Lambertian>(material.brdf)) {			
			lambertian = true;
			S72::Material::Lambertian lambert =
				std::get<S72::Material::Lambertian>(material.brdf);
			if (std::holds_alternative<S72::color>(lambert.albedo)) {
				S72::color albedo_color = std::get<S72::color>(lambert.albedo);

				if (texture_color_to_index.count(albedo_color) == 0) {
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = 1, .height = 1 },
						VK_FORMAT_R32G32B32_SFLOAT,
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						Helpers::Unmapped
					));

					rtg.helpers.transfer_to_image(&albedo_color, 12, textures.back());
					texture_color_to_index[albedo_color] = static_cast<uint32_t>(textures.size() - 1);
				}

			}
			else if (std::holds_alternative<S72::Texture*>(lambert.albedo)) {

			}
			else {
				throw std::runtime_error("Unsupported material type");
			}
		}
	}

	if (lambertian && !rtg.scene.environments.empty()) {
		int tex_width, tex_height, tex_channels;
		stbi_set_flip_vertically_on_load(false);
		auto path = rtg.scene.environments.begin()->second.radiance->path;
		std::string lambertian_env_path = path.substr(0, path.size() - 3) + "lambertian.my.png";
		unsigned char* image = stbi_load(lambertian_env_path.c_str(), &tex_width, &tex_height, &tex_channels, 4);
		if (image == nullptr) {
			throw std::runtime_error("Failed to load texture image: " + lambertian_env_path);
		}

		assert(tex_height % 6 == 0);

		for (size_t i = 0; i < tex_width * tex_height; i++) {
			size_t j = 4 * i;
			rgbe_to_e5b9g9r9(&image[j]);
		}

		textures.emplace_back(rtg.helpers.create_cubemap(
			VkExtent2D{ .width = static_cast<uint32_t>(tex_width), .height = static_cast<uint32_t>(tex_height) / 6 },
			VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		));

		rtg.helpers.transfer_to_cubemap(image, tex_width * tex_height * 4, textures.back());
		stbi_image_free(image);

	}
	

}

void Viewer::construct_bounding_boxes(const std::vector<Vertex>& vertices)
{
	mesh_bounding_boxes.reserve(mesh_vertices.size());
	for (const auto& [name, mesh_vertex] : mesh_vertices) {
		const Vertex& vertex = vertices[mesh_vertex.first];
		float min_x = vertex.Position.x;
		float max_x = vertex.Position.x;
		float min_y = vertex.Position.y;
		float max_y = vertex.Position.y;
		float min_z = vertex.Position.z;
		float max_z = vertex.Position.z;

		for (uint32_t i = 1; i < mesh_vertex.count; i++) {
			const Vertex& next_vertex = vertices[mesh_vertex.first + i];
			min_x = std::min(min_x, next_vertex.Position.x);
			max_x = std::max(max_x, next_vertex.Position.x);
			min_y = std::min(min_y, next_vertex.Position.y);
			max_y = std::max(max_y, next_vertex.Position.y);
			min_z = std::min(min_z, next_vertex.Position.z);
			max_z = std::max(max_z, next_vertex.Position.z);
		}

		mesh_bounding_boxes[name] = BoundingBox{
			.min_x = min_x, .min_y = min_y, .min_z = min_z,
			.max_x = max_x, .max_y = max_y, .max_z = max_z
		};
	}
}


