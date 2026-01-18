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

		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace
			}
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = per_workspace * 2,
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
		std::vector<PosNorTexVertex> vertices;

		{// a quadrilateral
			plane_vertices.first = static_cast<uint32_t>(vertices.size());
			vertices.emplace_back(PosNorTexVertex{
			.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f },
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f },
			.TexCoord{.s = 0.0f, .t = 0.0f },
				});
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 0.0f },
				});
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 1.0f },
				});
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = 1.0f, .y = 1.0f, .z = 0.0f },
					.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f },
					.TexCoord{.s = 1.0f, .t = 1.0f },
					});
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f },
					.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
					.TexCoord{.s = 0.0f, .t = 1.0f },
					});
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f },
					.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
					.TexCoord{.s = 1.0f, .t = 0.0f },
					});
				plane_vertices.count = static_cast<uint32_t>(vertices.size()) - plane_vertices.first;

		}

		{ //A torus:
			torus_vertices.first = uint32_t(vertices.size());

			//TODO: torus!
			constexpr float R1 = 0.75f;
			constexpr float R2 = 0.15f;

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16;

			constexpr float V_REPEATS = 2.0f;
			constexpr float U_REPEATS = int(V_REPEATS / R2 * R1 + 0.999f);

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				float ua = (ui % U_STEPS) / static_cast<float>(U_STEPS) * 2.0f * static_cast<float>(M_PI);
				float va = (vi % V_STEPS) / static_cast<float>(V_STEPS) * 2.0f * static_cast<float>(M_PI);

				vertices.emplace_back(PosNorTexVertex{
					.Position{
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
					});
				};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui, vi + 1);

					emplace_vertex(ui, vi + 1);
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui + 1, vi + 1);
				}
			}

			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;
		}


		//A single triangle
		/*vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 0.0f, .y = 0.0f, .z = 0.0f },
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f  },
			.TexCoord{.s = 0.0f, .t = 0.0f}
			});
		vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 1.0f, .y = 0.0f, .z = 0.0f },
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f  },
			.TexCoord{.s = 1.0f, .t = 0.0f}
			});
		vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 0.0f, .y = 1.0f, .z = 0.0f },
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f  },
			.TexCoord{.s = 0.0f, .t = 1.0f}
			});*/

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{
		textures.reserve(2);
		{ 
			uint32_t size = 128;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				float fy = (y + 0.5f) / float(size);
				for (uint32_t x = 0; x < size; ++x) {
					float fx = (x + 0.5f) / float(size);
					//highlight the origin:
					if (fx < 0.05f && fy < 0.05f) data.emplace_back(0xff0000ff); //red
					else if ((fx < 0.5f) == (fy < 0.5f)) data.emplace_back(0xff444444); //dark grey
					else data.emplace_back(0xffbbbbbb); //light grey
				}
			}
			assert(data.size() == size * size);

			//TODO: make a place for the texture to live on the GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size },
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));


			//TODO: transfer data
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0])* data.size(), textures.back());
		}

		{ //TODO: texture 1 will be a classic 'xor' texture
			uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size* size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
				}
			}
			assert(data.size() == size * size);

			//make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_SRGB, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}
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

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}

		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
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
			for (ObjectInstance const& inst : object_instances) {
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

		{

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

			{
				std::array<VkDescriptorSet, 1> descriptor_sets{
					workspace.Transforms_descriptors
				};

				vkCmdBindDescriptorSets(
					workspace.command_buffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					objects_pipeline.layout,
					1,
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

	{
		object_instances.clear();

		{
			mat4 WORLD_FROM_LOCAL{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = plane_vertices,
				.transform = {
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL
				}
			});

		}

		{
			float ang = time / 60.0f * 2.0f * float(M_PI) * 10.0f;
			float ca = std::cos(ang);
			float sa = std::sin(ang);
			mat4 WORLD_FROM_LOCAL{
				  ca, 0.0f,  -sa, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				  sa, 0.0f,   ca, 0.0f,
				-1.0f,0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 1
			});
		}
	}

	
}


void Tutorial::on_input(InputEvent const &) {
}
