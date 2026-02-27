
#include "RTG.hpp"

#include "Viewer.hpp"


#include "CubeUtility.hpp"

#include <iostream>

int main(int argc, char** argv) {
	//main wrapped in a try-catch so we can print some debug info about uncaught exceptions:
	try {

		//configure application:
		RTG::Configuration configuration;

		configuration.application_info = VkApplicationInfo{
			.pApplicationName = "cube utility",
			.applicationVersion = VK_MAKE_VERSION(0,0,0),
			.pEngineName = "Unknown",
			.engineVersion = VK_MAKE_VERSION(0,0,0),
			.apiVersion = VK_API_VERSION_1_3
		};

		bool print_usage = false;

		configuration.is_cube_utility = true;

		try {
			configuration.parse(argc, argv);
		}
		catch (std::runtime_error& e) {
			std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
			print_usage = true;
		}

		if (print_usage) {
			std::cerr << "Usage:" << std::endl;
			RTG::Configuration::usage([](const char* arg, const char* desc) {
				std::cerr << "    " << arg << "\n        " << desc << std::endl;
				});
			return 1;
		}

		//loads vulkan library, creates surface, initializes helpers:
		configuration.headless = true;
		RTG rtg(configuration);
		
		std::cout << "util" << std::endl;
		CubeUtility cube_util(rtg);
		std::cout << "process" << std::endl;
		cube_util.process_cubemap(rtg.configuration.in_cubemap_file, rtg.configuration.out_cubemap_file);	
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return 1;
	}
}
