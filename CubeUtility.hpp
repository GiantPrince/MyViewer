#pragma once
#include "RTG.hpp"
#include "CubePipeline.hpp"

class CubeUtility {
public:
	CubeUtility(RTG&);	
	~CubeUtility();

	CubeUtility(const CubeUtility&) = delete;
	CubeUtility& operator=(const CubeUtility&) = delete;

	void process_cubemap(const std::string&, const std::string&);

	void e5b9g9r9_to_rgbe(unsigned char*);
	void rgbe_to_e5b9g9r9(unsigned char*);

	using Push = CubePipeline::Push;

private:
	RTG& rtg_;
	CubePipeline cube_pipeline_;
	VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
	VkDescriptorSet input_image_descriptor_set = VK_NULL_HANDLE;
	VkDescriptorSet output_image_descriptor_set = VK_NULL_HANDLE;

	VkImageView input_image_view_ = VK_NULL_HANDLE;
	VkSampler sampler_ = VK_NULL_HANDLE;

	VkCommandPool command_pool_ = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;

	Helpers::AllocatedImage input_image_;
	std::vector<Helpers::AllocatedBuffer> output_images_;
};