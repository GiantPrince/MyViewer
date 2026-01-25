#include "RTG.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <vulkan/vulkan_core.h>
#if defined(__APPLE__)
#include <vulkan/vulkan_beta.h> //for portability subset
#include <vulkan/vulkan_metal.h> //for VK_EXT_METAL_SURFACE_EXTENSION_NAME
#endif
#include <vulkan/vk_enum_string_helper.h> //useful for debug output
#include <GLFW/glfw3.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>

void RTG::Configuration::parse(int argc, char** argv) {
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
		else {
			throw std::runtime_error("Unrecognized argument '" + arg + "'.");
		}
	}
}

void RTG::Configuration::usage(std::function< void(const char*, const char*) > const& callback) {
	callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--physical-device <name>", "Run on the named physical device (guesses, otherwise).");
	callback("--drawing-size <w> <h>", "Set the size of the surface to draw to.");
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

	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT	|
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debug_callback,
		.pUserData = nullptr
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

	//create the `window` and `surface` (where things get drawn):
	refsol::RTG_constructor_create_surface(
		configuration.application_info,
		configuration.debug,
		configuration.surface_extent,
		instance,
		&window,
		&surface
	);

	//select the `physical_device` -- the gpu that will be used to draw:
	refsol::RTG_constructor_select_physical_device(
		configuration.debug,
		configuration.physical_device_name,
		instance,
		&physical_device
	);

	//select the `surface_format` and `present_mode` which control how colors are represented on the surface and how new images are supplied to the surface:
	refsol::RTG_constructor_select_format_and_mode(
		configuration.debug,
		configuration.surface_formats,
		configuration.present_modes,
		physical_device,
		surface,
		&surface_format,
		&present_mode
	);

	//create the `device` (logical interface to the GPU) and the `queue`s to which we can submit commands:
	refsol::RTG_constructor_create_device(
		configuration.debug,
		physical_device,
		surface,
		&device,
		&graphics_queue_family,
		&graphics_queue,
		&present_queue_family,
		&present_queue
	);

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

	// determine size, image count, and transform for swapchain
	VkSurfaceCapabilitiesKHR capabilities;
	VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));

	swapchain_extent = capabilities.currentExtent;

	uint32_t requested_count = capabilities.minImageCount;
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
	/*refsol::RTG_destroy_swapchain(
		device,
		&swapchain,
		&swapchain_images,
		&swapchain_image_views,
		&swapchain_image_dones
	);*/

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

	if (swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		swapchain = VK_NULL_HANDLE;
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


	// time handling
	std::chrono::high_resolution_clock::time_point before = std::chrono::high_resolution_clock::now();

	// event handling
	std::vector<InputEvent> event_queue;
	glfwSetWindowUserPointer(window, &event_queue);
	glfwSetCursorPosCallback(window, cursor_pos_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetKeyCallback(window, key_callback);

	while (!glfwWindowShouldClose(window)) {
		//event handling:
		glfwPollEvents();

		for (InputEvent const& input : event_queue) {
			application.on_input(input);
		}
		event_queue.clear();

		{
			std::chrono::high_resolution_clock::time_point after = std::chrono::high_resolution_clock::now();
			float dt = float(std::chrono::duration<double>(after - before).count());
			before = after;
			dt = std::min(dt, 0.1f);
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

		// call render function
		application.render(*this, RenderParams{
			.workspace_index = workspace_index,
			.image_index = image_index,
			.image_available = workspaces[workspace_index].image_available,
			.image_done = swapchain_image_dones[image_index],
			.workspace_available = workspaces[workspace_index].workspace_available
			});

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

	//tear down event handling:
	glfwSetMouseButtonCallback(window, nullptr);
	glfwSetCursorPosCallback(window, nullptr);
	glfwSetScrollCallback(window, nullptr);
	glfwSetKeyCallback(window, nullptr);

	glfwSetWindowUserPointer(window, nullptr);
}
