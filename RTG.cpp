#include "RTG.hpp"



#include "VK.hpp"

#include <vulkan/vulkan_core.h>
#if defined(__APPLE__)
#include <vulkan/vulkan_beta.h> //for portability subset
#include <vulkan/vulkan_metal.h> //for VK_EXT_METAL_SURFACE_EXTENSION_NAME
#endif
#include <vulkan/vk_enum_string_helper.h> //useful for debug output
#include <vulkan/utility/vk_format_utils.h> //for getting format sizes
#include <GLFW/glfw3.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <set>

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.hpp"


void RTG::Configuration::parse(int argc, char** argv) {
	if (is_cube_utility) {
		if (argc != 4 && argc != 3) {
			throw std::runtime_error("Should have exactly 2 or 3 arguments.");
		}

		if (argc == 4) {
			in_cubemap_file = argv[1];
			out_cubemap_file = argv[3];
			if (!in_cubemap_file.ends_with(".png")) {
				throw std::runtime_error("Input cubemap file should be .png format.");
			}
			if (!out_cubemap_file.ends_with(".png")) {
				throw std::runtime_error("Output cubemap file should be .png format.");
			}

			std::string mode = argv[2];
			if (mode == "--ggx") {
				cube_util_mode = CubeUtilMode::GGX;
			}
			else if (mode == "--lambertian") {
				cube_util_mode = CubeUtilMode::LAMBERTIAN;
			}
			else {
				throw std::runtime_error("Unrecognized argument " + mode + ".");
			}
		}
		else {
			out_cubemap_file = argv[2];			
			if (!out_cubemap_file.ends_with(".lut")) {
				throw std::runtime_error("Output cubemap file should be .lut format.");
			}
			std::string mode = argv[1];
			assert(mode == "--lut");
			cube_util_mode = CubeUtilMode::LUT;
		}		
	}
	else {
		for (int argi = 1; argi < argc; ++argi) {
			std::string arg = argv[argi];
			if (arg == "--debug") {
				debug = true;
			}
			else if (arg == "--no-debug") {
				debug = false;
			}
			else if (arg == "--physical-device") {
				if (argi + 1 >= argc) throw std::runtime_error("--physical-device requires a parameter (a device name).");
				argi += 1;
				physical_device_name = argv[argi];
			}
			else if (arg == "--drawing-size") {
				if (argi + 2 >= argc) throw std::runtime_error("--drawing-size requires two parameters (width and height).");
				auto conv = [&](std::string const& what) {
					argi += 1;
					std::string val = argv[argi];
					for (size_t i = 0; i < val.size(); ++i) {
						if (val[i] < '0' || val[i] > '9') {
							throw std::runtime_error("--drawing-size " + what + " should match [0-9]+, got '" + val + "'.");
						}
					}
					return std::stoul(val);
					};
				surface_extent.width = conv("width");
				surface_extent.height = conv("height");
			}
			else if (arg == "--headless") {
				headless = true;
			}
			else if (arg == "--scene") {
				if (argi + 1 >= argc)
					throw std::runtime_error("--scene requires a parameter (a path to the scene file).");
				argi += 1;
				scene_file = argv[argi];
				if (!scene_file.ends_with(".s72")) {
					throw std::runtime_error("--scene parameter should be a .s72 file.");
				}
			}
			else if (arg == "--culling") {
				if (argi + 1 >= argc)
					throw std::runtime_error("--culling requires a parameter (culling mode).");
				argi += 1;
				std::string mode = argv[argi];
				if (mode == "none") {
					culling_mode = CullingMode::NONE;
				}
				else if (mode == "frustum") {
					culling_mode = CullingMode::FRUSTUM;
				}
				else {
					throw std::runtime_error("Unrecognized culling mode '" + mode + "'.");
				}
			}
			else if (arg == "--camera") {
				if (argi + 1 >= argc) {
					throw std::runtime_error("--camera requires a parameter (camera name).");
				}
				argi += 1;
				camera_name = argv[argi];
			}
			else if (arg == "--profile") {
				profile = true;
			}
			else if (arg == "--indexed") {
				indexed = true;
			}
			else if (arg == "--exposure") {
				if (argi + 1 >= argc) {
					throw std::runtime_error("--exposure requires a parameter (the value of exposure)");
				}
				argi += 1;
				exposure = std::stof(argv[argi]);
			}
			else if (arg == "--tone-map") {
				if (argi + 1 >= argc) {
					throw std::runtime_error("--tone-map requires a parameter (the operator name of tone mapping)");
				}
				argi += 1;
				std::string tone_op = argv[argi];
				if (tone_op == "linear") {
					tone_operator = ToneOperator::LINEAR;
				}
				else if (tone_op == "reinhard") {
					tone_operator = ToneOperator::REINHARD;
				}
				else {
					throw std::runtime_error("Unrecognized tone operator '" + tone_op + "'.");
				}
			}
			else if (arg == "--show-colliders") {
				show_colliders = true;
			}
			else {
				throw std::runtime_error("Unrecognized argument '" + arg + "'.");
			}
		}
	}
	
}

void RTG::Configuration::usage(std::function< void(const char*, const char*) > const& callback) {	
	callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--physical-device <name>", "Run on the named physical device (guesses, otherwise).");
	callback("--drawing-size <w> <h>", "Set the size of the surface to draw to.");
	callback("--headless", "Don't create a window; read events from stdin.");
	callback("--tone-map <op>", "Specify a tone mapping operator.");
	callback("--exposure <E>", "Set the value of exposure before tone operator.");
	callback("--camera <name>", "Set the initial scene camera to be the one named <name>.");
	callback("--profile", "Enable profiling on GPU.");
	callback("--indexed", "Enable indexed mesh.");
	callback("--show-colliders", "Show colliders as wireframe boxes.");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* data,
	void* user_data
) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		std::cerr << "\x1b[91m" << "E: ";
	}
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "\x1b[33m" << "w: ";
	}
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		std::cerr << "\x1b[90m" << "i: ";
	}
	else { //VERBOSE
		std::cerr << "\x1b[90m" << "v: ";
	}
	std::cerr << data->pMessage << "\x1b[0m" << std::endl;

	return VK_FALSE;
}

RTG::RTG(Configuration const& configuration_) : helpers(*this) {

	//copy input configuration:
	configuration = configuration_;

	//fill in flags/extensions/layers information:

	//create the `instance` (main handle to Vulkan library):

	VkInstanceCreateFlags instance_flags = 0;
	std::vector<const char*> instance_extensions;
	std::vector<const char*> instance_layers;

#if defined(__APPLE__)
	instance_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	instance_extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	instance_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
	instance_extensions.emplace_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif

	
	
	if (!configuration.is_cube_utility)
	{
		if (configuration.scene_file == "") {
			throw std::runtime_error("No scene file specified. Use --scene <file.s72> to specify a scene file.");
		}
		// load the scene
		try {
			scene = S72::load(configuration.scene_file);

			// check camera
			if (configuration.camera_name != "") {
				auto cam = scene.cameras.at(configuration.camera_name);
			}			
		}
		catch (std::exception const& e) {
			std::cerr << "Scene loading failed:\n" << e.what() << std::endl;
		}
	}

	if (!configuration.headless)
	{
		// add extensions needed by glfw
		glfwInit();
		if (!glfwVulkanSupported()) {
			throw std::runtime_error("GLFW reports Vulkan is not supported.");
		}

		uint32_t count;

		const char** extensions = glfwGetRequiredInstanceExtensions(&count);
		if (extensions == nullptr) {
			throw std::runtime_error("GLFW failed to return a list of requested instance extensions. Perhaps it was not compiled with Vulkan support.");
		}
		for (uint32_t i = 0; i < count; ++i) {
			instance_extensions.emplace_back(extensions[i]);
		}
	}

	// add extensions and layers for debugging
	if (configuration.debug) {
		instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		instance_layers.emplace_back("VK_LAYER_KHRONOS_validation");
	}

	VkValidationFeatureEnableEXT enables[] = { VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };
	VkValidationFeaturesEXT nfeatures{};
	nfeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	nfeatures.enabledValidationFeatureCount = 1;
	nfeatures.pEnabledValidationFeatures = enables;

	

	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = &nfeatures,
		.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debug_callback,
		.pUserData = nullptr,
		
	};

	VkInstanceCreateInfo instance_create_info{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = (configuration.debug ? &debug_messenger_create_info : nullptr),
		.flags = instance_flags,
		.pApplicationInfo = &configuration.application_info,
		.enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
		.ppEnabledLayerNames = instance_layers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
		.ppEnabledExtensionNames = instance_extensions.data()
	};
	VK(vkCreateInstance(&instance_create_info, nullptr, &instance));

	if (configuration.debug) {
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
			(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (!vkCreateDebugUtilsMessengerEXT) {
			throw std::runtime_error("Failed to lookup debug utils create fn.");
		}
		VK(vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_create_info, nullptr, &debug_messenger));
	}

	if (!configuration.headless)
		//create the `window` and `surface` (where things get drawn):	
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		window = glfwCreateWindow(
			configuration.surface_extent.width,
			configuration.surface_extent.height,
			configuration.application_info.pApplicationName,
			nullptr,
			nullptr
		);

		if (!window) {
			throw std::runtime_error("GLFW failed to create a window.");
		}

		VK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
	}

	//select the `physical_device` -- the gpu that will be used to draw:
	std::vector<std::string> physical_device_names;
	{
		uint32_t count;
		VK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
		std::vector<VkPhysicalDevice> physical_devices(count);
		VK(vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()));

		uint32_t best_score = 0;

		for (auto const& pd : physical_devices) {
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(pd, &properties);

			VkPhysicalDeviceFeatures features;
			vkGetPhysicalDeviceFeatures(pd, &features);

			physical_device_names.emplace_back(properties.deviceName);

			if (!configuration.physical_device_name.empty()) {
				if (configuration.physical_device_name == properties.deviceName) {
					if (physical_device) {
						std::cerr << "WARNING: have two physical devices with the name '" << properties.deviceName << "'; using the first to be enumerated." << std::endl;
					}
					else {
						physical_device = pd;
					}
				}
			}
			else {
				uint32_t score = 1;
				if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
					score += 0x8000;
				}

				if (score > best_score) {
					best_score = score;
					physical_device = pd;
				}
			}
		}
	}

	if (physical_device == VK_NULL_HANDLE) {
		// report error
		std::cerr << "Physical devices:\n";
		for (std::string const& name : physical_device_names) {
			std::cerr << "    " << name << "\n";
		}
		std::cerr.flush();

		if (!configuration.physical_device_name.empty()) {
			throw std::runtime_error("No physical device with name '" + configuration.physical_device_name + "'.");
		}
		else {
			throw std::runtime_error("No suitable GPU found.");
		}
	}

	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		std::cout << "Selected physical device: '" << properties.deviceName << "'." << std::endl;
		if (configuration.profile)
			timestamp_period = properties.limits.timestampPeriod;
	}

	//select the `surface_format` and `present_mode` which control how colors are represented on the surface and how new images are supplied to the surface:
	if (configuration.headless) {
		if (configuration.surface_formats.empty()) {
			throw std::runtime_error("No surface formats requested.");
		}

		surface_format = configuration.surface_formats[0];

		bool have_fifo = false;
		for (auto const& mode : configuration.present_modes) {
			if (mode == VK_PRESENT_MODE_FIFO_KHR) {
				have_fifo = true;
				break;
			}
		}
		if (!have_fifo) {
			throw std::runtime_error("Configured present modes do not contain VK_PRESENT_MODE_FIFO_KHR.");
		}
		present_mode = VK_PRESENT_MODE_FIFO_KHR;

		present_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
	else {
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> present_modes;

		{
			uint32_t count = 0;
			VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr));
			formats.resize(count);
			VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data()));
			std::cout << "Supported formats:" << std::endl;
			uint32_t index = 0;
			for (const auto& format : formats) {
				std::cout << "[" << index++ << "]\t" << string_VkFormat(format.format) << std::endl;
			}
		}

		{
			uint32_t count = 0;
			VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr));
			present_modes.resize(count);
			VK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, present_modes.data()));
			uint32_t index = 0;
			std::cout << "Supported present modes:" << std::endl;
			for (const auto& mode : present_modes) {
				std::cout << "[" << index++ << "]\t" << string_VkPresentModeKHR(mode) << std::endl;
			}
		}

		surface_format = [&]() {
			for (auto const& config_format : configuration.surface_formats) {
				for (const auto& format : formats) {
					if (config_format.format == format.format && config_format.colorSpace == format.colorSpace) {
						return format;
					}
				}
			}
			throw std::runtime_error("No format matching requested format(s) found.");
			}();

		present_mode = [&]() {
			for (auto const& config_mode : configuration.present_modes) {
				for (auto const& mode : present_modes) {
					if (config_mode == mode) {
						return mode;
					}
				}
			}
			throw std::runtime_error("No present mode matching requested mode(s) found.");
			}();
	}

	//create the `device` (logical interface to the GPU) and the `queue`s to which we can submit commands:
	{
		{
			// look up queue indices
			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
			std::vector<VkQueueFamilyProperties> queue_families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_families.data());

			for (auto const& queue_family : queue_families) {
				uint32_t i = uint32_t(&queue_family - &queue_families[0]);
				if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
					if (!graphics_queue_family)
						graphics_queue_family = i;
				}

				if (!configuration.headless) {
					VkBool32 present_support = VK_FALSE;
					VK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support));
					if (present_support == VK_TRUE) {
						if (!present_queue_family) {
							present_queue_family = i;
						}
					}
				}				
			}

		}

		if (configuration.headless) {
			present_queue_family = graphics_queue_family;
		}
		if (!graphics_queue_family) {
			throw std::runtime_error("No queue with graphics support.");
		}

		if (!present_queue_family) {
			throw std::runtime_error("No queue with present support.");
		}

		std::vector<const char*> device_extensions;

		{
#if defined(__APPLE__)
			device_extensions.emplace_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
			if (!configuration.headless) {
				device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			}
			
		}

		{
			std::vector<VkDeviceQueueCreateInfo> queue_create_info;
			std::set<uint32_t> unique_queue_families{
				graphics_queue_family.value(),
				present_queue_family.value()
			};

			float queue_priorities[1] = { 1.0f };

			for (uint32_t queue_family : unique_queue_families) {
				queue_create_info.emplace_back(
					VkDeviceQueueCreateInfo{
						.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						.queueFamilyIndex = queue_family,
						.queueCount = 1,
						.pQueuePriorities = queue_priorities
					}
				);

			}

			/*VkPhysicalDeviceVulkan12Features features12{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
				.pNext = nullptr,				
				.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,				
				.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
				.descriptorBindingPartiallyBound = VK_TRUE,
				.runtimeDescriptorArray = VK_TRUE
			};*/
			device_extensions.emplace_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
			VkDeviceCreateInfo device_create_info{
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,				
				.queueCreateInfoCount = static_cast<uint32_t>(queue_create_info.size()),
				.pQueueCreateInfos = queue_create_info.data(),
				.enabledLayerCount = 0,
				.ppEnabledLayerNames = nullptr,
				.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
				.ppEnabledExtensionNames = device_extensions.data(),
				.pEnabledFeatures = nullptr
			};

			VK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));
			vkGetDeviceQueue(device, graphics_queue_family.value(), 0, &graphics_queue);
			vkGetDeviceQueue(device, present_queue_family.value(), 0, &present_queue);
		}
	}

	//run any resource creation required by Helpers structure:
	helpers.create();

	//create initial swapchain:
	recreate_swapchain();

	//create workspace resources:
	workspaces.resize(configuration.workspaces);
	for (auto& workspace : workspaces) {
		//refsol::RTG_constructor_per_workspace(device, &workspace);
		{
			//create workspace fences
			VkFenceCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags = VK_FENCE_CREATE_SIGNALED_BIT
			};

			VK(vkCreateFence(device, &create_info, nullptr, &workspace.workspace_available));
		}

		{
			VkSemaphoreCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
			};
			VK(vkCreateSemaphore(device, &create_info, nullptr, &workspace.image_available));

		}
	}

}
RTG::~RTG() {
	//don't destroy until device is idle:
	if (device != VK_NULL_HANDLE) {
		if (VkResult result = vkDeviceWaitIdle(device); result != VK_SUCCESS) {
			std::cerr << "Failed to vkDeviceWaitIdle in RTG::~RTG [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
		}
	}

	//destroy workspace resources:
	for (auto& workspace : workspaces) {
		//refsol::RTG_destructor_per_workspace(device, &workspace);
		if (workspace.workspace_available != VK_NULL_HANDLE) {
			vkDestroyFence(device, workspace.workspace_available, nullptr);
			workspace.workspace_available = VK_NULL_HANDLE;
		}
		if (workspace.image_available != VK_NULL_HANDLE) {
			vkDestroySemaphore(device, workspace.image_available, nullptr);
			workspace.image_available = VK_NULL_HANDLE;
		}
	}
	workspaces.clear();

	//destroy the swapchain:
	destroy_swapchain();

	//destroy Helpers structure resources:
	helpers.destroy();

	//destroy the rest of the resources:
	//refsol::RTG_destructor( &device, &surface, &window, &debug_messenger, &instance );
	if (device != VK_NULL_HANDLE) {
		vkDestroyDevice(device, nullptr);
		device = VK_NULL_HANDLE;
	}

	if (surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(instance, surface, nullptr);
		surface = VK_NULL_HANDLE;
	}

	if (window != VK_NULL_HANDLE) {
		glfwDestroyWindow(window);
		window = nullptr;
	}

	if (debug_messenger != VK_NULL_HANDLE) {
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT =
			(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (vkDestroyDebugUtilsMessengerEXT) {
			vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
			debug_messenger = VK_NULL_HANDLE;
		}
	}

	if (instance != VK_NULL_HANDLE) {
		vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}
}


void RTG::recreate_swapchain() {
	/*refsol::RTG_recreate_swapchain(
		configuration.debug,
		device,
		physical_device,
		surface,
		surface_format,
		present_mode,
		graphics_queue_family,
		present_queue_family,
		&swapchain,
		&swapchain_extent,
		&swapchain_images,
		&swapchain_image_views,
		&swapchain_image_dones
	);*/
	// clean up swapchain if already exists
	if (!swapchain_images.empty()) {
		destroy_swapchain();
	}

	if (configuration.headless) {
		assert(surface == VK_NULL_HANDLE);

		//make a fake swapchain:

		//set extent from configuration
		swapchain_extent = configuration.surface_extent;
		//swapchain_extent = VkExtent2D{ .width = 3840, .height = 2160  };

		//set number of images to 3
		uint32_t requested_count = 3;

		//create headless_command_pool
		{
			assert(headless_command_pool == VK_NULL_HANDLE);
			VkCommandPoolCreateInfo command_pool_create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = 0,
			.queueFamilyIndex = graphics_queue_family.value(),
			};

			VK(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &headless_command_pool));
		}

		//create headless_swapchain
		assert(headless_swapchain.empty());
		headless_swapchain.reserve(requested_count);
		for (uint32_t i = 0; i < requested_count; i++) {
			HeadlessSwapchainImage& h = headless_swapchain.emplace_back();

			//allocate image data			
			h.image = helpers.create_image(
				swapchain_extent,
				surface_format.format,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);

			//allocate buffer data
			h.buffer = helpers.create_buffer(
				swapchain_extent.width * swapchain_extent.height * vkuFormatTexelBlockSize(surface_format.format) / vkuFormatTexelsPerBlock(surface_format.format),
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				Helpers::Mapped
			);

			//create and record copy command
			{
				VkCommandBufferAllocateInfo alloc_info{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
					.commandPool = headless_command_pool,
					.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
					.commandBufferCount = 1,
				};

				VK(vkAllocateCommandBuffers(device, &alloc_info, &h.copy_command));

				VkCommandBufferBeginInfo begin_info{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = 0
				};

				VK(vkBeginCommandBuffer(h.copy_command, &begin_info));

				VkBufferImageCopy region{
					.bufferOffset = 0,
					.bufferRowLength = swapchain_extent.width,
					.bufferImageHeight = swapchain_extent.height,
					.imageSubresource{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = 0,
						.baseArrayLayer = 0,
						.layerCount = 1
					},
					.imageOffset{.x = 0, .y = 0, .z = 0 },
					.imageExtent{
						.width = swapchain_extent.width,
						.height = swapchain_extent.height,
						.depth = 1
					}
				};

				vkCmdCopyImageToBuffer(
					h.copy_command,
					h.image.handle,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					h.buffer.handle,
					1,
					&region
				);

				VK(vkEndCommandBuffer(h.copy_command));


			}

			{
				VkFenceCreateInfo create_info{
					.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
					.flags = VK_FENCE_CREATE_SIGNALED_BIT
				};

				VK(vkCreateFence(device, &create_info, nullptr, &h.image_presented));
			}


		}


		//fill in swapchain_images
		assert(swapchain_images.empty());
		swapchain_images.assign(requested_count, VK_NULL_HANDLE);
		for (uint32_t i = 0; i < requested_count; i++) {
			swapchain_images[i] = headless_swapchain[i].image.handle;
		}


	}
	else {
		assert(surface != VK_NULL_HANDLE);

		// determine size, image count, and transform for swapchain
		VkSurfaceCapabilitiesKHR capabilities;
		VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));

		swapchain_extent = capabilities.currentExtent;

		uint32_t requested_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount != 0) {
			requested_count = std::min(requested_count, capabilities.maxImageCount);
		}

		{	// create swapchain
			VkSwapchainCreateInfoKHR create_info{
				.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
				.surface = surface,
				.minImageCount = requested_count,
				.imageFormat = surface_format.format,
				.imageColorSpace = surface_format.colorSpace,
				.imageExtent = swapchain_extent,
				.imageArrayLayers = 1,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				.preTransform = capabilities.currentTransform,
				.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				.presentMode = present_mode,
				.clipped = VK_TRUE,
				.oldSwapchain = VK_NULL_HANDLE
			};

			std::vector<uint32_t> queue_family_indices{
				graphics_queue_family.value(),
				present_queue_family.value()
			};

			if (queue_family_indices[0] != queue_family_indices[1]) {
				//if images will be presented on a different queue, make sure they are shared:
				create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
				create_info.pQueueFamilyIndices = queue_family_indices.data();
			}
			else {
				create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			VK(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain));
		}

		{
			// get swapchain images
			uint32_t count = 0;
			VK(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
			swapchain_images.resize(count);
			VK(vkGetSwapchainImagesKHR(device, swapchain, &count, swapchain_images.data()));

		}

	}

	// create image views
	swapchain_image_views.assign(swapchain_images.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain_image_views.size(); i++) {
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = surface_format.format,
			.components{
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY
			},
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VK(vkCreateImageView(device, &create_info, nullptr, &swapchain_image_views[i]));
	}

	{
		VkSemaphoreCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};

		swapchain_image_dones.assign(swapchain_images.size(), VK_NULL_HANDLE);
		for (size_t i = 0; i < swapchain_image_dones.size(); i++) {
			VK(vkCreateSemaphore(device, &create_info, nullptr, &swapchain_image_dones[i]));
		}
	}

	if (configuration.debug) {
		std::cout << "Swapchain is now " << swapchain_images.size() << " images of size " << swapchain_extent.width << "x" << swapchain_extent.height << "." << std::endl;
	}
}


void RTG::destroy_swapchain() {

	VK(vkDeviceWaitIdle(device));

	for (auto& semaphore : swapchain_image_dones) {
		vkDestroySemaphore(device, semaphore, nullptr);
		semaphore = VK_NULL_HANDLE;
	}
	swapchain_image_dones.clear();

	for (auto& image_view : swapchain_image_views) {
		vkDestroyImageView(device, image_view, nullptr);
		image_view = VK_NULL_HANDLE;
	}
	swapchain_image_views.clear();

	swapchain_images.clear();

	if (configuration.headless) {
		for (auto& h : headless_swapchain) {
			helpers.destroy_image(std::move(h.image));
			helpers.destroy_buffer(std::move(h.buffer));
			h.copy_command = VK_NULL_HANDLE;
			vkDestroyFence(device, h.image_presented, nullptr);
			h.image_presented = VK_NULL_HANDLE;
		}
		headless_swapchain.clear();

		vkDestroyCommandPool(device, headless_command_pool, nullptr);
		headless_command_pool = VK_NULL_HANDLE;
	}
	else {
		if (swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
	}
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
	std::vector<InputEvent>* event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if (!event_queue)
		return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	event.type = InputEvent::MouseMotion;
	event.motion.x = float(xpos);
	event.motion.y = float(ypos);
	event.motion.state = 0;

	for (int b = 0; b < 8 && b < GLFW_MOUSE_BUTTON_LAST; b++) {
		if (glfwGetMouseButton(window, b) == GLFW_PRESS) {
			event.motion.state |= (1 << b);
		}
	}

	event_queue->emplace_back(event);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	std::vector<InputEvent>* event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if (!event_queue)
		return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	if (action == GLFW_PRESS) {
		event.type = InputEvent::MouseButtonDown;
	}
	else if (action == GLFW_RELEASE) {
		event.type = InputEvent::MouseButtonUp;
	}
	else {
		std::cerr << "Strange: unknown mouse button action." << std::endl;
		return;
	}

	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	event.button.x = float(xpos);
	event.button.y = float(ypos);
	event.button.state = 0;
	for (int b = 0; b < 8 && b < GLFW_MOUSE_BUTTON_LAST; ++b) {
		if (glfwGetMouseButton(window, b) == GLFW_PRESS) {
			event.button.state |= (1 << b);
		}
	}
	event.button.button = uint8_t(button);
	event.button.mods = uint8_t(mods);

	event_queue->emplace_back(event);
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	std::vector<InputEvent>* event_queue = reinterpret_cast<std::vector<InputEvent>*>(glfwGetWindowUserPointer(window));
	if (!event_queue)
		return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	event.type = InputEvent::MouseWheel;
	event.wheel.x = float(xoffset);
	event.wheel.y = float(yoffset);

	event_queue->emplace_back(event);

}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	std::vector< InputEvent >* event_queue = reinterpret_cast<std::vector< InputEvent > *>(glfwGetWindowUserPointer(window));
	if (!event_queue) return;

	InputEvent event;
	std::memset(&event, '\0', sizeof(event));

	if (action == GLFW_PRESS) {
		event.type = InputEvent::KeyDown;
	}
	else if (action == GLFW_RELEASE) {
		event.type = InputEvent::KeyUp;
	}
	else if (action == GLFW_REPEAT) {
		//ignore repeats
		return;
	}
	else {
		std::cerr << "Strange: got unknown keyboard action." << std::endl;
	}

	event.key.key = key;
	event.key.mods = mods;

	event_queue->emplace_back(event);
}

void RTG::run(Application& application) {
	// refsol::RTG_run(*this, application);
	// initial on_swapchain
	auto on_swapchain = [&, this]() {
		application.on_swapchain(*this, SwapchainEvent{
			.extent = swapchain_extent,
			.images = swapchain_images,
			.image_views = swapchain_image_views
			});
		};

	on_swapchain();

	// headless next image
	uint32_t headless_next_image = 0;

	// time handling
	std::chrono::high_resolution_clock::time_point before = std::chrono::high_resolution_clock::now();

	// event handling
	std::vector<InputEvent> event_queue;

	if (!configuration.headless) {
		glfwSetWindowUserPointer(window, &event_queue);
		glfwSetCursorPosCallback(window, cursor_pos_callback);
		glfwSetMouseButtonCallback(window, mouse_button_callback);
		glfwSetScrollCallback(window, scroll_callback);
		glfwSetKeyCallback(window, key_callback);
	}


	while (configuration.headless || !glfwWindowShouldClose(window)) {

		float headless_dt = 0.0f;
		std::string headless_save = "";

		//event handling:
		if (configuration.headless) {
			// read events from stdin
			std::string line;
			while (std::getline(std::cin, line)) {
				try {
					std::istringstream iss(line);
					iss.imbue(std::locale::classic());

					std::string type;
					if (!(iss >> type)) {
						throw std::runtime_error("failed to read event type");
					}

					if (type == "AVAILABLE") {
						//read dt
						if (!(iss >> headless_dt)) {
							throw std::runtime_error("failed to read dt.");
						}
						if (headless_dt < 0) {
							throw std::runtime_error("dt less than zero.");
						}

						//check for save file name
						if (iss >> headless_save) {
							if (!headless_save.ends_with(".png")) throw std::runtime_error("output filename ("" + headless_save + "") must end with .ppm");
						}

						//check for trailing junk
						char junk;
						if (iss >> junk) throw std::runtime_error("trailing junk in event line");


						//stop parsing events so a frame can draw
						break;
					}
					else {
						throw std::runtime_error("unrecognized type");
					}
				}
				catch (std::exception& e) {
					std::cerr << "WARNING: failed to parse event (" << e.what() << ") from: "" << line << ""; ignoring it." << std::endl;
				}



			}

			if (!std::cin) {
				break;
			}
		}
		else {
			glfwPollEvents();
		}


		for (InputEvent const& input : event_queue) {
			application.on_input(input);
		}
		event_queue.clear();

		{
			std::chrono::high_resolution_clock::time_point after = std::chrono::high_resolution_clock::now();
			float dt = float(std::chrono::duration<double>(after - before).count());
			before = after;
			dt = std::min(dt, 0.1f);
			if (configuration.headless) {
				dt = headless_dt;
			}
			application.update(dt);
		}

		uint32_t workspace_index;
		{
			assert(next_workspace < workspaces.size());
			workspace_index = next_workspace;
			next_workspace = (next_workspace + 1) % workspaces.size();
			VK(vkWaitForFences(device, 1, &workspaces[workspace_index].workspace_available, VK_TRUE, UINT64_MAX));
			VK(vkResetFences(device, 1, &workspaces[workspace_index].workspace_available));

		}

		uint32_t image_index = -1U;

		if (configuration.headless) {
			assert(swapchain == VK_NULL_HANDLE);
			assert(headless_next_image < static_cast<uint32_t>(headless_swapchain.size()));
			image_index = headless_next_image;
			headless_next_image = (headless_next_image + 1) % uint32_t(headless_swapchain.size());

			//wait for image to be done copying to buffer
			VK(vkWaitForFences(device, 1, &headless_swapchain[image_index].image_presented, VK_TRUE, UINT64_MAX));

			//TODO: save buffer, if needed
			if (headless_swapchain[image_index].save_to != "") {
				headless_swapchain[image_index].save();
				headless_swapchain[image_index].save_to = "";
			}

			headless_swapchain[image_index].save_to = headless_save;

			//mark next copy as pending
			VK(vkResetFences(device, 1, &headless_swapchain[image_index].image_presented));

			//TODO: signal GPU that image is "available for rendering to"
			VkSubmitInfo submit_info{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = &workspaces[workspace_index].image_available
			};

			VK(vkQueueSubmit(graphics_queue, 1, &submit_info, nullptr));


		}
		else {

		retry:
			if (VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, workspaces[workspace_index].image_available, VK_NULL_HANDLE, &image_index);
				result == VK_ERROR_OUT_OF_DATE_KHR) {
				std::cerr << "Recreating swapchain because vkAcquireNextImageKHR returned " << string_VkResult(result) << "." << std::endl;

				recreate_swapchain();
				on_swapchain();

				goto retry;
			}
			else if (result == VK_SUBOPTIMAL_KHR) {
				std::cerr << "Suboptimal swapchain format -- ignoring for the moment." << std::endl;
			}
			else if (result != VK_SUCCESS) {
				throw std::runtime_error("Failed to acquire swapchain image (" + std::string(string_VkResult(result)) + ")!");
			}

		}
		// call render function
		application.render(*this, RenderParams{
			.workspace_index = workspace_index,
			.image_index = image_index,
			.image_available = workspaces[workspace_index].image_available,
			.image_done = swapchain_image_dones[image_index],
			.workspace_available = workspaces[workspace_index].workspace_available
			});

		if (configuration.headless) {
			VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			VkSubmitInfo submit_info{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &swapchain_image_dones[image_index],
				.pWaitDstStageMask = &wait_stage,
				.commandBufferCount = 1,
				.pCommandBuffers = &headless_swapchain[image_index].copy_command
			};

			VK(vkQueueSubmit(graphics_queue, 1, &submit_info, headless_swapchain[image_index].image_presented));
		}
		else
		{
			// queue the work for presentation
			VkPresentInfoKHR present_info{
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &swapchain_image_dones[image_index],
				.swapchainCount = 1,
				.pSwapchains = &swapchain,
				.pImageIndices = &image_index,
			};

			assert(present_queue);

			if (VkResult result = vkQueuePresentKHR(present_queue, &present_info);
				result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
				std::cerr << "Recreating swapchain because vkQueuePresentKHR returned " << string_VkResult(result) << "." << std::endl;
				recreate_swapchain();
				on_swapchain();
			}
			else if (result != VK_SUCCESS) {
				throw std::runtime_error("failed to queue presentation of image (" + std::string(string_VkResult(result)) + ")!");
			}
		}

	}

	if (configuration.headless) {
		for (size_t i = 0; i < headless_swapchain.size(); i++) {
			uint32_t image_index = headless_next_image;
			headless_next_image = (headless_next_image + 1) % uint32_t(headless_swapchain.size());

			VK(vkWaitForFences(device, 1, &headless_swapchain[image_index].image_presented, VK_TRUE, UINT64_MAX));

			//save if requested:
			if (headless_swapchain[image_index].save_to != "") {
				headless_swapchain[image_index].save();
				headless_swapchain[image_index].save_to = "";
			}
		}
	}

	//tear down event handling:
	if (!configuration.headless) {
		glfwSetMouseButtonCallback(window, nullptr);
		glfwSetCursorPosCallback(window, nullptr);
		glfwSetScrollCallback(window, nullptr);
		glfwSetKeyCallback(window, nullptr);

		glfwSetWindowUserPointer(window, nullptr);
	}

}

void RTG::HeadlessSwapchainImage::save() const {
	if (save_to == "") {
		return;
	}

	if (image.format == VK_FORMAT_B8G8R8A8_SRGB) {
		//get a pointer to the image data copied to the buffer:
		char const* bgra = reinterpret_cast<char const*>(buffer.allocation.data());

		//convert bgra -> rgb data
		std::vector<char> rgb(image.extent.height * image.extent.width * 3);
		for (uint32_t y = 0; y < image.extent.height; y++) {
			for (uint32_t x = 0; x < image.extent.width; x++) {
				uint32_t texel_index = y * image.extent.width + x;
				rgb[texel_index * 3 + 0] = bgra[texel_index * 4 + 2];
				rgb[texel_index * 3 + 1] = bgra[texel_index * 4 + 1];
				rgb[texel_index * 3 + 2] = bgra[texel_index * 4 + 0];
			}
		}

		std::vector<unsigned char> image_data(rgb.begin(), rgb.end());
		if (stbi_write_png(save_to.c_str(), image.extent.width, image.extent.height, 3, image_data.data(), image.extent.width * 3)) {
			std::cout << "write image data to " << save_to << std::endl;
		}
		else {
			std::cout << "failed to write data to " << save_to << std::endl;
		}
		//write ppm file
		/*std::ofstream ppm(save_to, std::ios::binary);
		ppm << "P6\n";
		ppm << image.extent.width << " " << image.extent.height << "\n";
		ppm << "255\n";
		ppm.write(rgb.data(), rgb.size());*/
	}
	else {
		std::cerr << "WARNING: saving format " << string_VkFormat(image.format) << " not supported." << std::endl;
	}
}

