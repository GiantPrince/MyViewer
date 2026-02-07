#include "Tutorial.hpp"

#include "VK.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>


Tutorial::Tutorial(RTG& rtg_) : rtg(rtg_) {
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
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);

		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
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

	{
		load_textures();
	}

	{
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const& image : textures) {
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				}
			};
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
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
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
	}
	else {
		camera_mode = CameraMode::Free;
	}


}

Tutorial::~Tutorial() {

	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
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

void Tutorial::on_swapchain(RTG& rtg_, RTG::SwapchainEvent const& swapchain) {
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

void Tutorial::destroy_framebuffers() {
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


bool Tutorial::is_mesh_in_frustum(const std::string& name, const BoundingBox& box, const mat4& WORLD_FROM_LOCAL)
{
	float z_near = free_camera.near;
	float z_far = free_camera.far;

	float y_near = std::tan(free_camera.fov / 2.0f) * z_near;
	float x_near = y_near * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height;

	vec4 corners[4] = {
		vec4{ box.min_x, box.min_y, box.min_z, 1.0f },
		vec4{ box.max_x, box.min_y, box.min_z, 1.0f },
		vec4{ box.min_x, box.max_y, box.min_z, 1.0f },
		vec4{ box.min_x, box.min_y, box.max_z, 1.0f }
	};

	for (int i = 0; i < 4; i++) {
		corners[i] = orbit(free_camera.target_x, free_camera.target_y, free_camera.target_z, free_camera.azimuth, free_camera.elevation, free_camera.radius) * WORLD_FROM_LOCAL * corners[i];
	}

	vec4 axes1 = corners[1] - corners[0];
	vec4 axes2 = corners[2] - corners[0];
	vec4 axes3 = corners[3] - corners[0];

	vec4 center = corners[0] + (axes1 + axes2 + axes3) / 2.0f;
	float x_extent = std::sqrt(axes1[0] * axes1[0] + axes1[1] * axes1[1] + axes1[2] * axes1[2]);
	float y_extent = std::sqrt(axes2[0] * axes2[0] + axes2[1] * axes2[1] + axes2[2] * axes2[2]);
	float z_extent = std::sqrt(axes3[0] * axes3[0] + axes3[1] * axes3[1] + axes3[2] * axes3[2]);

	axes1 = axes1 / x_extent;
	axes2 = axes2 / y_extent;
	axes3 = axes3 / z_extent;

	x_extent /= 2.0f;
	y_extent /= 2.0f;
	z_extent /= 2.0f;

	{
		/*float M_x = 0;
		float M_y = 0;
		float M_z = 1.0f;*/

		//float MoX = 0.0f;
		//float MoY = 0.0f;
		//float MoZ = 1.0f;

		float MoC = center[2];

		float radius = 0.0f;
		radius += std::fabsf(axes1[2]) * x_extent;
		radius += std::fabsf(axes2[2]) * y_extent;
		radius += std::fabsf(axes3[2]) * z_extent;
		
		float z_min = MoC - radius;
		float z_max = MoC + radius;
		if (z_max < -z_far || z_min > -z_near) {
			if (name == "Room") {
				std::cout << name << std::endl;
				std::cout << z_min << " " << z_max << std::endl;
			}
			
			return false;
		}
	}

	{
		const vec4 M[] = {
			 { 0.0,z_near, y_near, 0.0f },
			{ 0.0, -z_near, y_near, 0.0f },
			{ z_near, 0.0f, x_near, 0.0f },
			{ -z_near, 0.0f, x_near, 0.0f },
		};

		for (size_t m = 0; m < 4; m++) {
			float MoX = std::fabsf(M[m][0]);
			float MoY = std::fabsf(M[m][1]);
			float MoZ = M[m][2];
			float MoC = M[m][0] * center[0] + M[m][1] * center[1] + M[m][2] * center[2];

			float radius = 0.0f;
			radius += std::fabsf(M[m][0] * axes1[0] + M[m][1] * axes1[1] + M[m][2] * axes1[2]) * x_extent;
			radius += std::fabsf(M[m][0] * axes2[0] + M[m][1] * axes2[1] + M[m][2] * axes2[2]) * y_extent;
			radius += std::fabsf(M[m][0] * axes3[0] + M[m][1] * axes3[1] + M[m][2] * axes3[2]) * z_extent;

			
			float obb_min = MoC - radius;
			float obb_max = MoC + radius;

			float p = x_near * MoX + y_near * MoY;

			float tau_0 = z_near * MoZ - p;
			float tau_1 = z_near * MoZ + p;

			if (tau_0 < 0.0f) {
				tau_0 *= z_far / z_near;
			}
			if (tau_1 > 0.0f) {
				tau_1 *= z_far / z_near;
			}

			if (obb_max < -tau_1 || obb_min > -tau_0) {		
				if (name == "Room") {
					std::cout << name << std::endl;
					
				}
				return false;
			}
		}
	}
	return true;
}

void Tutorial::draw_frustum()
{
	float ca = std::cos(free_camera.azimuth);
	float sa = std::sin(free_camera.azimuth);
	float ce = std::cos(free_camera.elevation);
	float se = std::sin(free_camera.elevation);

	//compute right direction
	float right_x = -sa;
	float right_y = ca;
	float right_z = 0.0f;

	//compute up direction
	float up_x = -se * ca;
	float up_y = -se * sa;
	float up_z = ce;

	//compute out direction
	float out_x = ce * ca;
	float out_y = ce * sa;
	float out_z = se;

	float cam_x = free_camera.target_x + free_camera.radius * out_x;
	float cam_y = free_camera.target_y + free_camera.radius * out_y;
	float cam_z = free_camera.target_z + free_camera.radius * out_z;

	float near_y = std::tan(free_camera.fov / 2.0f) * free_camera.near - 0.01f;
	float near_x = near_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height - 0.01f;

	float far_y = std::tan(free_camera.fov / 2.0f) * free_camera.far - 0.01f;
	float far_x = far_y * rtg.swapchain_extent.width / (float)rtg.swapchain_extent.height -0.01f;

	free_camera.near += 0.01f;
	free_camera.far -= 0.01f;	
	float near_top_left_x = cam_x + (-out_x * free_camera.near) + (up_x * near_y) - (right_x * near_x);
	float near_top_left_y = cam_y + (-out_y * free_camera.near) + (up_y * near_y) - (right_y * near_x);
	float near_top_left_z = cam_z + (-out_z * free_camera.near) + (up_z * near_y) - (right_z * near_x);

	float near_top_right_x = cam_x + (-out_x * free_camera.near) + (up_x * near_y) + (right_x * near_x);
	float near_top_right_y = cam_y + (-out_y * free_camera.near) + (up_y * near_y) + (right_y * near_x);
	float near_top_right_z = cam_z + (-out_z * free_camera.near) + (up_z * near_y) + (right_z * near_x);

	float near_bottom_left_x = cam_x + (-out_x * free_camera.near) + (-up_x * near_y) - (right_x * near_x);
	float near_bottom_left_y = cam_y + (-out_y * free_camera.near) + (-up_y * near_y) - (right_y * near_x);
	float near_bottom_left_z = cam_z + (-out_z * free_camera.near) + (-up_z * near_y) - (right_z * near_x);

	float near_bottom_right_x = cam_x + (-out_x * free_camera.near) + (-up_x * near_y) + (right_x * near_x);
	float near_bottom_right_y = cam_y + (-out_y * free_camera.near) + (-up_y * near_y) + (right_y * near_x);
	float near_bottom_right_z = cam_z + (-out_z * free_camera.near) + (-up_z * near_y) + (right_z * near_x);

	float far_top_left_x = cam_x + (-out_x * free_camera.far) + (up_x * far_y) - (right_x * far_x);
	float far_top_left_y = cam_y + (-out_y * free_camera.far) + (up_y * far_y) - (right_y * far_x);
	float far_top_left_z = cam_z + (-out_z * free_camera.far) + (up_z * far_y) - (right_z * far_x);

	float far_top_right_x = cam_x + (-out_x * free_camera.far) + (up_x * far_y) + (right_x * far_x);
	float far_top_right_y = cam_y + (-out_y * free_camera.far) + (up_y * far_y) + (right_y * far_x);
	float far_top_right_z = cam_z + (-out_z * free_camera.far) + (up_z * far_y) + (right_z * far_x);

	float far_bottom_left_x = cam_x + (-out_x * free_camera.far) + (-up_x * far_y) - (right_x * far_x);
	float far_bottom_left_y = cam_y + (-out_y * free_camera.far) + (-up_y * far_y) - (right_y * far_x);
	float far_bottom_left_z = cam_z + (-out_z * free_camera.far) + (-up_z * far_y) - (right_z * far_x);

	float far_bottom_right_x = cam_x + (-out_x * free_camera.far) + (-up_x * far_y) + (right_x * far_x);
	float far_bottom_right_y = cam_y + (-out_y * free_camera.far) + (-up_y * far_y) + (right_y * far_x);
	float far_bottom_right_z = cam_z + (-out_z * free_camera.far) + (-up_z * far_y) + (right_z * far_x);

	free_camera.near -= 0.01f;
	free_camera.far += 0.01f;

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
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = cam_x, .y = cam_y, .z = cam_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = free_camera.target_x + right_x + up_x, .y = free_camera.target_y + right_y + up_y, .z = free_camera.target_z + right_z + up_z},
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = cam_x, .y = cam_y, .z = cam_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});
	lines_vertices.emplace_back(PosColVertex{
		.Position = {.x = cam_x - out_x * 2 - right_x - up_x, .y = cam_y - out_y * 2 - right_y - up_y, .z = cam_z - out_z * 2 - right_z - up_z },
		.Color = {0.0f, 0.0f, 1.0f, 1.0f}
		});

}

void Tutorial::render(RTG& rtg_, RTG::RenderParams const& render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace& workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	//record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	//refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);

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
			LinesPipeline::Camera camera{
				.CLIP_FROM_WORLD = CLIP_FROM_WORLD
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
			for (ObjectInstance & inst : object_instances) {
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
			LinesPipeline::Camera camera{
				.CLIP_FROM_WORLD = CLIP_FROM_WORLD
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

		{

		}

		//objects
		{
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{
				std::array<VkBuffer, 1> vertex_buffers{ mesh_vertex_buffer.handle };
				std::array<VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, static_cast<uint32_t>(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

				//Camera descriptor set is still bound(!)

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

			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);

				vkCmdBindDescriptorSets(
					workspace.command_buffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					objects_pipeline.layout,
					2,
					1, &texture_descriptors[inst.texture],
					0, nullptr
				);


				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
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


void Tutorial::update(float dt) {
	time += dt;

	

	lines_vertices.clear();

	{
		load_objects();
	}

	if (camera_mode == CameraMode::Scene)
	{		
		CLIP_FROM_WORLD = perspective(
			scene_camera.fov,
			scene_camera.aspect,
			scene_camera.near,
			scene_camera.far
		) * look_at(
			scene_camera.eye_x, scene_camera.eye_y, scene_camera.eye_z,
			scene_camera.eye_x + scene_camera.forward_x, scene_camera.eye_y + scene_camera.forward_y, scene_camera.eye_z + scene_camera.forward_z,
			scene_camera.up_x, scene_camera.up_y, scene_camera.up_z
		);
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
		
	}
	else {
		assert(false && "invalid camera mode");
	}

	if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM)
	{
		draw_frustum();
	}
	

	

}


void Tutorial::on_input(InputEvent const& evt) {	
	if (action) {
		action(evt);
		return;
	}

	// general
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		previous_camera_mode = camera_mode;
		camera_mode = CameraMode((int(camera_mode) + 1) % 3);
		return;
	}

	// free camera control
	if (camera_mode == CameraMode::Free) {
		if (evt.type == InputEvent::MouseWheel) {
			free_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
			free_camera.radius = std::min(free_camera.radius, 2.0f * free_camera.far);
			free_camera.radius = std::max(free_camera.radius, 0.5f * free_camera.near);
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			//start panning
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;

			action = [this, init_x, init_y, init_camera](InputEvent const& evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					//cancel upon button lifted:
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					// Image height at the plane of the target point
					float height = 2.0f * std::tan(free_camera.fov * 0.5f) * free_camera.radius;

					// motion at target point
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
					float dy = (evt.motion.y - init_y) / rtg.swapchain_extent.height * height;

					//compute camera transform to extract right (first row) and up (second row):
					mat4 camera_from_world = orbit(
						init_camera.target_x, init_camera.target_y, init_camera.target_z,
						init_camera.azimuth, init_camera.elevation, init_camera.radius
					);

					//move the desired distance:
					free_camera.target_x = init_camera.target_x - dx * camera_from_world[0] - dy * camera_from_world[1];
					free_camera.target_y = init_camera.target_y - dx * camera_from_world[4] - dy * camera_from_world[5];
					free_camera.target_z = init_camera.target_z - dx * camera_from_world[8] - dy * camera_from_world[9];

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
			OrbitCamera init_camera = free_camera;

			action = [this, init_x, init_y, init_camera](InputEvent const& evt) {
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

					free_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					free_camera.elevation = init_camera.elevation - dy * speed;

					const float twopi = 2.0f * float(M_PI);
					free_camera.azimuth -= std::round(free_camera.azimuth / twopi) * twopi;
					free_camera.elevation -= std::round(free_camera.elevation / twopi) * twopi;
					return;
				}
				};
			return;
		}
	}
}

std::vector<Vertex> Tutorial::load_mesh_vertices() {
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
		assert(stride = mesh.attributes.at("TANGENT").stride);
		assert(stride = mesh.attributes.at("TEXCOORD").stride);

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

		mesh_vertices[name] = MeshVertices{
			.first = first,
			.count = count
		};
	}

	return vertices;
}

void Tutorial::load_objects() {
	object_instances.clear();
	for (const auto root : rtg.scene.scene.roots) {

		const mat4 identity = mat4{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};


		load_objects(root, identity, identity);
	}
}

void Tutorial::load_objects(const S72::Node* root, const mat4& world_from_local, const mat4& world_from_local_normal) {
	// compute parent from local

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

	if (camera_mode == CameraMode::Scene && root->camera != nullptr) {
		assert(root->camera->name == rtg.configuration.camera_name);
		if (std::holds_alternative<S72::Camera::Perspective>(root->camera->projection)) {
			S72::Camera::Perspective perspective = std::get<S72::Camera::Perspective>(root->camera->projection);
			scene_camera.fov = perspective.vfov;
			scene_camera.near = perspective.near;
			scene_camera.far = perspective.far;
			scene_camera.aspect = perspective.aspect;
		}
		vec4 cam_pos = WORLD_FROM_LOCAL * vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
		scene_camera.eye_x = cam_pos[0];
		scene_camera.eye_y = cam_pos[1];
		scene_camera.eye_z = cam_pos[2];

		vec4 forward = WORLD_FROM_LOCAL_NORMAL * vec4{ 0.0f, 0.0f, -1.0f, 0.0f };
		scene_camera.forward_x = forward[0];
		scene_camera.forward_y = forward[1];
		scene_camera.forward_z = forward[2];

		vec4 up = WORLD_FROM_LOCAL_NORMAL * vec4{ 0.0f, 1.0f, 0.0f, 0.0f };
		scene_camera.up_x = up[0];
		scene_camera.up_y = up[1];
		scene_camera.up_z = up[2];
	}

	if (root->light != nullptr) {
		if (std::holds_alternative<S72::Light::Sun>(root->light->source)) {
			S72::Light::Sun sun = std::get<S72::Light::Sun>(root->light->source);
			assert(sun.angle == 0 || sun.angle == float(M_PI));

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
			throw std::runtime_error("Unsupported light type");
		}
	}

	if (root->mesh != nullptr) {
		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::NONE
			|| (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM 
				&& is_mesh_in_frustum(root->mesh->name, mesh_bounding_boxes[root->mesh->name], WORLD_FROM_LOCAL))) {
			uint32_t texture_index = 0;

			if (root->mesh->material != nullptr) {
				// add object instance
				if (std::holds_alternative<S72::Material::Lambertian>(root->mesh->material->brdf)) {
					S72::Material::Lambertian lambert = std::get<S72::Material::Lambertian>(root->mesh->material->brdf);
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
				else {
					throw std::runtime_error("Unsupported material type");
				}
			}
			object_instances.emplace_back(ObjectInstance{
					.vertices = mesh_vertices.at(root->mesh->name),
					.transform = {
						//.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL
					},
					.texture = texture_index

				});

			
		}

		if (rtg.configuration.culling_mode == RTG::Configuration::CullingMode::FRUSTUM) {
			
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

	// recurse to children
	for (const auto child : root->children) {
		load_objects(child, WORLD_FROM_LOCAL, WORLD_FROM_LOCAL_NORMAL);
	}
}

void Tutorial::load_textures() {
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

	for (const auto& [name, texture] : rtg.scene.textures) {
		int tex_width, tex_height, tex_channels;
		unsigned char* image = stbi_load(texture.path.c_str(), &tex_width, &tex_height, &tex_channels, 0);
		if (image == nullptr) {
			throw std::runtime_error("Failed to load texture image: " + texture.path);
		}

		assert(tex_channels == 3);
		VkFormat format = VK_FORMAT_UNDEFINED;

		if (texture.format == S72::Texture::Format::linear) {
			format = VK_FORMAT_R8G8B8_UNORM;
		}
		else if (texture.format == S72::Texture::Format::srgb) {
			format = VK_FORMAT_R8G8B8_SRGB;
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


	for (const auto& [name, material] : rtg.scene.materials) {
		if (std::holds_alternative<S72::Material::Lambertian>(material.brdf)) {
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
					rtg.helpers.transfer_to_image(&lambert.albedo, 12, textures.back());
					texture_color_to_index[albedo_color] = static_cast<uint32_t>(textures.size() - 1);
				}

			}
			else {
				throw std::runtime_error("Unsupported material type");
			}
		}
	}
}

void Tutorial::construct_bounding_boxes(const std::vector<Vertex>& vertices)
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


