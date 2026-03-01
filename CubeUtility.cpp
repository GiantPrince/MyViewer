#include "CubeUtility.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.hpp"

#define __STDC_LIB_EXT1__

#include "stb_image_write.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>


void rgbe_to_float_vector(
	const unsigned char* input,
	size_t pixelCount,
	std::vector<float>& output)
{
	output.resize(pixelCount * 4);

	for (size_t i = 0; i < pixelCount; ++i)
	{
		const unsigned char* rgbe = input + i * 4;

		unsigned char r = rgbe[0];
		unsigned char g = rgbe[1];
		unsigned char b = rgbe[2];
		unsigned char e = rgbe[3];

		float* out = &output[i * 4];

		if (e == 0)
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			out[3] = 1.0f;
		}
		else
		{
			

			float scale = std::ldexp(1.0f, int(e) - 128 - 8);

			out[0] = (r + 0.5f) * scale;
			out[1] = (g + 0.5f) * scale;
			out[2] = (b + 0.5f) * scale;
			out[3] = 1.0f;
		}
	}
}

void float_to_rgbe(const float* rgb, unsigned char* rgbe)
{
	float r = rgb[0];
	float g = rgb[1];
	float b = rgb[2];

	float maxRGB = std::max(r, std::max(g, b));

	if (maxRGB < 1e-32f) {
		rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
		return;
	}

	int exp;
	std::frexp(maxRGB, &exp);  

	float denom = std::ldexp(1.0f, exp - 8);

	rgbe[0] = (unsigned char)std::clamp(int(rgb[0] / denom + 0.5f), 0, 255);
	rgbe[1] = (unsigned char)std::clamp(int(rgb[1] / denom + 0.5f), 0, 255);
	rgbe[2] = (unsigned char)std::clamp(int(rgb[2] / denom + 0.5f), 0, 255);
	rgbe[3] = (unsigned char)(exp + 128);
}



CubeUtility::CubeUtility(RTG& rtg) : rtg_(rtg) {
	cube_pipeline_.create(rtg);

	{
		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1
			}
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 2,
			.poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data()
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool_));
	}

	if (rtg.configuration.cube_util_mode != RTG::Configuration::CubeUtilMode::LUT)
	{
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool_,
			.descriptorSetCount = 1,
			.pSetLayouts = &cube_pipeline_.set0_inputImageCube,
		};

		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &input_image_descriptor_set));
	}

	{
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool_,
			.descriptorSetCount = 1,
			.pSetLayouts = &cube_pipeline_.set1_outputImageCube
		};

		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &output_image_descriptor_set));
	}

	{
		VkCommandPoolCreateInfo command_pool_create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value()
		};

		vkCreateCommandPool(rtg.device, &command_pool_create_info, nullptr, &command_pool_);

		VkCommandBufferAllocateInfo command_buffer_alloc_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = command_pool_,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};

		vkAllocateCommandBuffers(rtg.device, &command_buffer_alloc_info, &command_buffer_);

	}

	if (rtg.configuration.cube_util_mode != RTG::Configuration::CubeUtilMode::LUT)
	{
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
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

		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &sampler_));
	}

}

CubeUtility::~CubeUtility() {
	cube_pipeline_.destroy(rtg_);


	if (descriptor_pool_ != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(rtg_.device, descriptor_pool_, nullptr);
		descriptor_pool_ = VK_NULL_HANDLE;
	}

	if (input_image_view_ != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg_.device, input_image_view_, nullptr);
		input_image_view_ = VK_NULL_HANDLE;
	}

	if (sampler_ != VK_NULL_HANDLE) {
		vkDestroySampler(rtg_.device, sampler_, nullptr);
		sampler_ = VK_NULL_HANDLE;
	}


	rtg_.helpers.destroy_image(std::move(input_image_));
	for (auto& image : output_images_) {
		rtg_.helpers.destroy_buffer(std::move(image));
	}

	output_images_.clear();

	if (command_pool_ != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg_.device, command_pool_, nullptr);
		command_pool_ = VK_NULL_HANDLE;
	}

}

void CubeUtility::process_cubemap(const std::string& input_cubemap, const std::string& output_cubemap)
{
	if (input_cubemap.empty()) {
		assert(rtg_.configuration.cube_util_mode == RTG::Configuration::CubeUtilMode::LUT);
		int width = 400;
		int height = 400;
		{
			output_images_.reserve(1);
			
			output_images_.emplace_back(rtg_.helpers.create_buffer(
				width * height * 8,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			VkDescriptorBufferInfo buffer_info{
				.buffer = output_images_[0].handle,
				.offset = 0,
				.range = output_images_[0].size
			};

			std::array< VkWriteDescriptorSet, 1> writes{
			VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = output_image_descriptor_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &buffer_info
				}
			};

			vkUpdateDescriptorSets(
				rtg_.device,
				static_cast<uint32_t>(writes.size()),
				writes.data(),
				0,
				nullptr
			);
		}

		{
			VkCommandBufferBeginInfo begin_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};

			vkBeginCommandBuffer(command_buffer_, &begin_info);

			vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, cube_pipeline_.handle);

			std::array<VkDescriptorSet, 1> descriptor_sets{				
				output_image_descriptor_set
			};
			vkCmdBindDescriptorSets(
				command_buffer_,
				VK_PIPELINE_BIND_POINT_COMPUTE,
				cube_pipeline_.layout,
				0,
				static_cast<uint32_t>(descriptor_sets.size()),
				descriptor_sets.data(),
				0, nullptr
			);
			
			assert(width % 8 == 0);
			assert(height % 8 == 0);
			vkCmdDispatch(command_buffer_, (width / 8), (height / 8), 1);
			vkEndCommandBuffer(command_buffer_);
		}

		{
			VkSubmitInfo submit_info{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &command_buffer_,
			};
			VK(vkQueueSubmit(rtg_.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
			VK(vkQueueWaitIdle(rtg_.graphics_queue));
		}

		std::vector<unsigned char> data;
		rtg_.helpers.transfer_buffer_to_vector(output_images_[0], data);

		std::ofstream out(output_cubemap, std::ios::binary);
		if (!out)
			throw std::runtime_error("failed to open lut file " + output_cubemap);
				
		out.write(reinterpret_cast<const char*>(&width), sizeof(width));
		out.write(reinterpret_cast<const char*>(&height), sizeof(height));
		out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(char));

		std::cout << *(reinterpret_cast<float*>(data.data())) << std::endl;
		out.close();
		

		std::cout << output_cubemap << std::endl;

		std::vector<uint8_t> png_data(width * height * 3); // RGB 8-bit
		float* d = reinterpret_cast<float*>(data.data());
		for (int i = 0; i < width * height; ++i)
		{			
			float r = d[i * 2 + 0];
			float g = d[i * 2 + 1];
			
			r = std::clamp(r, 0.0f, 1.0f);
			g = std::clamp(g, 0.0f, 1.0f);
			
			png_data[i * 3 + 0] = (uint8_t)std::min(255.0f, std::round(r * 255.0f));
			png_data[i * 3 + 1] = (uint8_t)std::min(255.0f, std::round(g * 255.0f));
			png_data[i * 3 + 2] = 0; // B channel fill 0	

			
		}

		stbi_write_png("brdf.png", width, height, 3, png_data.data(), 3 * width);
		return;

	}
	int tex_width, tex_height, tex_channels;
	stbi_set_flip_vertically_on_load(false);
	unsigned char* image = stbi_load(input_cubemap.c_str(), &tex_width, &tex_height, &tex_channels, 4);

	if (image == nullptr) {
		throw std::runtime_error("Can not read cubemap from " + input_cubemap);
	}

	std::vector<float> imagedata;
	rgbe_to_float_vector(image, tex_width* tex_height, imagedata);

	assert(tex_height % 6 == 0);

	/*for (size_t i = 0; i < tex_width * tex_height; i++) {
		size_t j = 4 * i;
		rgbe_to_e5b9g9r9(&image[j]);
	}*/

	input_image_ = rtg_.helpers.create_cubemap(
		VkExtent2D{ .width = static_cast<uint32_t>(tex_width), .height = static_cast<uint32_t>(tex_height) / 6 },
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);
	std::cout << tex_height << std::endl;

	rtg_.helpers.transfer_to_cubemap(reinterpret_cast<char *>(imagedata.data()), tex_width* tex_height * 4 * 4, input_image_);
	stbi_image_free(image);

	//create image view
	{
		VkImageViewCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0,
		.image = input_image_.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = input_image_.format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6
		}
		};
		VK(vkCreateImageView(rtg_.device, &create_info, nullptr, &input_image_view_));
	}

	size_t mipmap_levels = static_cast<size_t>(std::log2(std::min(tex_width, tex_height / 6)) + 1);

	{
		output_images_.reserve(mipmap_levels);
		int mipmap_width = tex_width / 2;
		int mipmap_height = tex_height / 2 / 6;
		for (size_t i = 0; i < mipmap_levels; i++) {
			output_images_.emplace_back(rtg_.helpers.create_buffer(
				mipmap_width * mipmap_height * 6 * 12,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));

			if (mipmap_width > 1) {
				mipmap_width /= 2;
			}
			if (mipmap_height > 1) {
				mipmap_height /= 2;
			}
		}

	}

	{
		VkDescriptorImageInfo image_info{
		.sampler = sampler_,
		.imageView = input_image_view_,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};


		std::array< VkWriteDescriptorSet, 1> writes{
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = input_image_descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &image_info
			}
		};

		vkUpdateDescriptorSets(
			rtg_.device,
			static_cast<uint32_t>(writes.size()),
			writes.data(),
			0,
			nullptr
		);
	}


	int mipmap_width = tex_width / 2;
	int mipmap_height = tex_height / 2 / 6;
	for (size_t i = 0; i < mipmap_levels - 1; i++) {
		VkDescriptorBufferInfo buffer_info{
			.buffer = output_images_[i].handle,
			.offset = 0,
			.range = output_images_[i].size
		};


		std::array< VkWriteDescriptorSet, 1> writes{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = output_image_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &buffer_info
		}
		};

		vkUpdateDescriptorSets(
			rtg_.device,
			static_cast<uint32_t>(writes.size()),
			writes.data(),
			0,
			nullptr
		);


		{
			VkCommandBufferBeginInfo begin_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};

			vkBeginCommandBuffer(command_buffer_, &begin_info);

			vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE, cube_pipeline_.handle);

			std::array<VkDescriptorSet, 2> descriptor_sets{
				input_image_descriptor_set,
				output_image_descriptor_set
			};
			vkCmdBindDescriptorSets(
				command_buffer_,
				VK_PIPELINE_BIND_POINT_COMPUTE,
				cube_pipeline_.layout,
				0,
				static_cast<uint32_t>(descriptor_sets.size()),
				descriptor_sets.data(),
				0, nullptr
			);

			Push push{
				.roughness = (i + 1) / static_cast<float>(mipmap_levels - 1),
				.faceSize = static_cast<uint32_t>(mipmap_width),
				.numOfSamples = 4096 / static_cast<uint32_t>(std::pow(2, std::min(4ULL, i - 1))),
				.mipmapLevel = static_cast<uint32_t>(i + 1)
			};

			vkCmdPushConstants(
				command_buffer_,
				cube_pipeline_.layout,
				VK_SHADER_STAGE_COMPUTE_BIT,
				0,
				sizeof(Push),
				&push
			);

			vkCmdDispatch(command_buffer_, ((mipmap_width + 7) / 8), ((mipmap_width + 7) / 8), 1);
			vkEndCommandBuffer(command_buffer_);
		}
		{
			VkSubmitInfo submit_info{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &command_buffer_,
			};
			VK(vkQueueSubmit(rtg_.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
			VK(vkQueueWaitIdle(rtg_.graphics_queue));
		}
		{
			std::vector<unsigned char> data;
			rtg_.helpers.transfer_buffer_to_vector(output_images_[i], data);
			float* d = reinterpret_cast<float*>(data.data());

			size_t size = data.size() / 3 / 4;

			std::vector<unsigned char> pngData(size * 4);

			for (size_t k = 0; k < size; k++)
			{
				float_to_rgbe(&d[k * 3], &pngData[k * 4]);
			}

			std::string out_file = (output_cubemap.substr(0, output_cubemap.size() - 3) + std::to_string(i + 1) + ".png");
			int result = stbi_write_png(
				out_file.c_str(),
				mipmap_width,
				mipmap_height * 6,
				4,
				pngData.data(),
				mipmap_width * 4
			);

			std::cout << out_file << std::endl;

			if (!result) {
				throw std::runtime_error("Failed to write.");
			}

			if (mipmap_width > 1) {
				mipmap_width /= 2;
			}
			if (mipmap_height > 1) {
				mipmap_height /= 2;
			}

		}
	}


}

void CubeUtility::e5b9g9r9_to_rgbe(unsigned char* pixel) {

	uint32_t packed =
		(uint32_t)pixel[0] |
		((uint32_t)pixel[1] << 8) |
		((uint32_t)pixel[2] << 16) |
		((uint32_t)pixel[3] << 24);

	if (packed == 0)
		return;

	uint32_t r9 = packed & 0x1FF;
	uint32_t g9 = (packed >> 9) & 0x1FF;
	uint32_t b9 = (packed >> 18) & 0x1FF;
	uint32_t e5 = (packed >> 27) & 0x1F;

	float scale = std::ldexp(1.0f, (int)e5 - 15) / 512.0f;

	float r = r9 * scale;
	float g = g9 * scale;
	float b = b9 * scale;

	float maxRGB = std::fmax(r, std::fmax(g, b));

	int exp;
	float norm = std::frexp(maxRGB, &exp);

	float scaleRGB = norm * 256.0f / maxRGB;

	pixel[0] = (unsigned char)(r * scaleRGB);
	pixel[1] = (unsigned char)(g * scaleRGB);
	pixel[2] = (unsigned char)(b * scaleRGB);
	pixel[3] = (unsigned char)(exp + 128);

}

void CubeUtility::rgbe_to_e5b9g9r9(unsigned char* rgbe)
{
	if (rgbe[0] == 0 && rgbe[1] == 0 && rgbe[2] == 0 && rgbe[3] == 0) {
		return;
	}

	uint32_t e = rgbe[3];
	uint32_t b = rgbe[2];
	uint32_t g = rgbe[1];
	uint32_t r = rgbe[0];

	float scale = std::ldexp(1.0f, e - 128 - 8);
	float fr = (r + 0.5f) * scale;
	float fg = (g + 0.5f) * scale;
	float fb = (b + 0.5f) * scale;

	float max_c = std::max(fr, std::max(fg, fb));

	int shared_exp;
	std::frexp(max_c, &shared_exp);
	

	int biased_exp = std::max(0, std::min(31, shared_exp + 15));

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
}
