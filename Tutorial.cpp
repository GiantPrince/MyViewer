#include "Tutorial.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{
		uint32_t per_workspace = static_cast<uint32_t>(rtg.workspaces.size());

		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace
			}
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = per_workspace * 1,
			.poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data()
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));

	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);

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

		{
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size
			};

			std::array< VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info
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

	{
		std::vector<PosColVertex> vertices;

		//A single triangle
		vertices.emplace_back(PosColVertex{
			.Position{.x = 0.0f, .y = 0.0f, .z = 0.0f },
			.Color{.r = 0xff, .g = 0xff, .b = 0xff, .a = 0xff  },
			});
		vertices.emplace_back(PosColVertex{
			.Position{.x = 1.0f, .y = 0.0f, .z = 0.0f },
			.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff },
			});
		vertices.emplace_back(PosColVertex{
			.Position{.x = 0.0f, .y = 1.0f, .z = 0.0f },
			.Color{.r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff  },
			});

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	
}

Tutorial::~Tutorial() {
	
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);

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
	}
	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers() {
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());
	
	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
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

	
	//render pass
	{
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color = {.float32{1.0f, 0.5f, 1.0f, 1.0f}} },
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
				std::array<VkBuffer, 1> vertex_buffers{ object_vertices.handle };
				std::array<VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, static_cast<uint32_t>(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

				//Camera descriptor set is still bound(!)

			}
			vkCmdDraw(workspace.command_buffer, static_cast<uint32_t>(object_vertices.size / sizeof(PosColVertex)), 1, 0, 0);

		}


		vkCmdEndRenderPass(workspace.command_buffer);
	}


	VK(vkEndCommandBuffer(workspace.command_buffer));
	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	time += dt;

	{
		float ang = float(M_PI) * 2.0f * 20.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI) / 180.0f,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),
			0.1f,
			1000.0f
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 3.0f * std::sin(ang),
			0.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f
		);

	}
	lines_vertices.clear();	

	// Lissajous
	/*float Ax = 0.5f;
	float Ay = 1.0f;
	float Az = 0.75f;

	float a = 3.0f;
	float b = 4.0f;
	float c = 7.0f;

	float aa = a;
	float bb = b;
	float cc = c;

	float phi_x = 0.5f * sin(0.7f * time);
	float phi_y = 0.5f * sin(1.1f * time + 1.3f);
	float phi_z = 0.5f * sin(1.7f * time + 2.1f);


	float step = 0.01f;
	for (float t = 0; t < 3 * 5 * 11 * 2 * float(M_PI); t += step) {
		float x_t = Ax * std::sin(aa * t + phi_x);
		float x_t_1 = Ax * std::sin(aa * (t + step) + phi_x);
		float y_t = Ay * std::sin(bb * t + phi_y);
		float y_t_1 = Ay * std::sin(bb * (t + step) + phi_y);
		float z_t = Az * std::sin(cc * t + phi_z);
		float z_t_1 = Az * std::sin(cc * (t + step) + phi_z);
		lines_vertices.emplace_back(PosColVertex{
			.Position{.x = x_t, .y = y_t, .z = z_t},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});
		lines_vertices.emplace_back(PosColVertex{
			.Position{.x = x_t_1, .y = y_t_1, .z = z_t_1},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
			});

	}*/
	float R = 1.0f;
	float r = 0.35f;
	float p = 3.0f;
	float q = 97.0f;

	float step = 0.01f;
	for (float t = 0; t < 2 * float(M_PI); t += step) {
		float x_t = (R + r * std::cos(q * t)) * std::cos(p * t);
		float y_t = (R + r * std::cos(q * t)) * std::sin(p * t);
		float z_t = r * std::sin(q * t);

		float x_t_1 = (R + r * std::cos(q * (t + step))) * std::cos(p * (t + step));
		float y_t_1 = (R + r * std::cos(q * (t + step))) * std::sin(p * (t + step));
		float z_t_1 = r * std::sin(q * (t + step));

		lines_vertices.emplace_back(PosColVertex{
			.Position{.x = x_t, .y = y_t, .z = z_t},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
			});
		lines_vertices.emplace_back(PosColVertex{
			.Position{.x = x_t_1, .y = y_t_1, .z = z_t_1},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
			});
	}

	
}


void Tutorial::on_input(InputEvent const &) {
}
