#pragma once

#include "RTG.hpp"

#include "PosColVertex.hpp"
#include "Vertex.hpp"
#include "PosNorTexVertex.hpp"
#include "mat4.hpp"

#include <GLFW/glfw3.h>


namespace std {
	template<>
	struct hash<S72::color> {
		size_t operator()(const S72::color& color) const noexcept {
			size_t h1 = std::hash<float>{}(color.r);
			size_t h2 = std::hash<float>{}(color.g);
			size_t h3 = std::hash<float>{}(color.b);
			return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2))
				^ (h3 + 0x9e3779b97f4a7c15ULL + (h2 << 6) + (h2 >> 2));
		}
	};
}

struct Tutorial : RTG::Application {

	Tutorial(RTG &);
	Tutorial(Tutorial const &) = delete; //you shouldn't be copying this object
	~Tutorial();

	//kept for use in destructor:
	RTG &rtg;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:
	struct BackgroundPipeline {
		// no descriptor sets

		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;
		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} background_pipeline;

	struct LinesPipeline {
		// descriptor set layouts
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		// types for descriptors
		struct Camera {
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 4 * 16, "camera buffer structure is packed.");
		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings
		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} lines_pipeline;

	// objects
	struct ObjectsPipeline{
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;

		struct Transform {
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16 * 4 + 16 * 4 + 16 * 4, "Transform is the expected size.");

		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY;
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY;
		};

		static_assert(sizeof(World) == 4 * 4 + 4 * 4 + 4 * 4 + 4 * 4, "World is the expected size.");		

		using Camera = LinesPipeline::Camera;

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = Vertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} objects_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	
	
	//workspaces hold per-render resources:
	
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.

		// location for lines data 
		Helpers::AllocatedBuffer line_vertices_src; // host coherent
		Helpers::AllocatedBuffer line_vertices;		// device local
		
		// locations for Camera data
		Helpers::AllocatedBuffer Camera_src;
		Helpers::AllocatedBuffer Camera;
		VkDescriptorSet Camera_descriptors;

		// location for World data
		Helpers::AllocatedBuffer World_src;
		Helpers::AllocatedBuffer World;
		VkDescriptorSet World_descriptors;

		// location for Transform data
		Helpers::AllocatedBuffer Transforms_src;
		Helpers::AllocatedBuffer Transforms;
		VkDescriptorSet Transforms_descriptors;

	};
	std::vector< Workspace > workspaces;

	//-------------------------------------------------------------------
	//static scene resources:
	Helpers::AllocatedBuffer object_vertices;
	struct ObjectVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};

	Helpers::AllocatedBuffer mesh_vertex_buffer;
	struct MeshVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};
	std::unordered_map<std::string, MeshVertices> mesh_vertices;

	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;

	std::vector<Helpers::AllocatedImage> textures;
	std::vector<VkImageView> texture_views;
	std::unordered_map<std::string, uint32_t> texture_name_to_index;	

	std::unordered_map<S72::color, uint32_t> texture_color_to_index;

	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> texture_descriptors;


	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	std::function<void(InputEvent const&)> action;

	float time = 0.0f;

	enum class CameraMode {
		Scene = 0,
		Free = 1
	} camera_mode = CameraMode::Free;

	//used when camera_mode == CameraMode::Free:
	struct OrbitCamera {
		float target_x = 0, target_y = 0, target_z = 0;
		float radius = 2.0f;
		float azimuth = 0.0f;
		float elevation = 0.25 * float(M_PI);
		float fov = 60.0f * float(M_PI) / 180.0f;
		float near = 0.1f;
		float far = 1000.0f;
	} free_camera;

	//computed from the current camera (as set by camera_mode) during update():
	mat4 CLIP_FROM_WORLD;

	std::vector<LinesPipeline::Vertex> lines_vertices;

	struct ObjectInstance {
		MeshVertices vertices;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;
	};
	std::vector<ObjectInstance> object_instances;
	
	
	ObjectsPipeline::World world;


	// loading all mesh indices
	std::vector<Vertex> load_mesh_vertices();

	// loading all objects
	void load_objects();
	void load_objects(const S72::Node* root, const mat4& world_from_local, const mat4& world_from_local_normal);

	// loading all textures
	void load_textures();

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
