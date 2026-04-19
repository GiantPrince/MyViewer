#pragma once

#include "RTG.hpp"

#include "PosColVertex.hpp"
#include "Vertex.hpp"
#include "PosNorTexVertex.hpp"
#include "Physics.hpp"
#include "mat4.hpp"
#include "solver.hpp"

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

struct Viewer : RTG::Application {

	Viewer(RTG &);
	Viewer(Viewer const &) = delete; //you shouldn't be copying this object
	~Viewer();

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
		VkDescriptorSetLayout set3_Camera = VK_NULL_HANDLE;
		VkDescriptorSetLayout set4_Environment = VK_NULL_HANDLE;
		VkDescriptorSetLayout set5_NormalMap = VK_NULL_HANDLE;
		
		VkDescriptorSetLayout set6_Roughness = VK_NULL_HANDLE;
		
		VkDescriptorSetLayout set7_Metalness = VK_NULL_HANDLE;
		
		VkDescriptorSetLayout set8_PreFilteredEnvironmentMap = VK_NULL_HANDLE;
		VkDescriptorSetLayout set9_BRDFLookupTable = VK_NULL_HANDLE;

		VkDescriptorSetLayout set10_Light = VK_NULL_HANDLE;
		VkDescriptorSetLayout set11_ShadowMaps = VK_NULL_HANDLE;

		struct Light {
			mat4 CLIP_FROM_WORLD;
			struct { float x, y, z; } direction;
			float angle;

			struct { float x, y, z; } position;
			float radius;

			struct { float r, g, b; } strength;
			float limit;

			float fov;
			float blend;
			enum class Type : uint32_t {
				SUN = 0,
				SPHERE = 1,
				SPOT = 2
			} type;			
			uint32_t shadow_map_index = UINT32_MAX;
			
		};
		
		static_assert(sizeof(Light) == 4 * 4 * 4 + 4 * 4 * 4, "Light is the expected size.");

		struct Camera {
			mat4 CLIP_FROM_WORLD;
			vec4 EYE;
		};

		struct Transform {
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16 * 4 + 16 * 4 + 16 * 4, "Transform is the expected size.");

		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY = { 0, 0, 0, 0 };
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY = { 0, 0, 0, 0 };			
		};

		static_assert(sizeof(World) == 4 * 4 + 4 * 4 + 4 * 4 + 4 * 4, "World is the expected size.");		

		//using Camera = LinesPipeline::Camera;

		//push constants
		struct Push {
			float exposure;
			int tone_operator;
			int light_count;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = Vertex;

		VkPipeline handle = VK_NULL_HANDLE;
		VkPipeline env_handle = VK_NULL_HANDLE;
		VkPipeline mirror_handle = VK_NULL_HANDLE;
		VkPipeline lambertian_env_handle = VK_NULL_HANDLE;
		VkPipeline pbr_handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} objects_pipeline;

	struct ShadowMapPipeline {

		VkDescriptorSetLayout set0_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Lights = VK_NULL_HANDLE;

		struct Push {
			uint32_t light_index;
		};

		struct Transform {			
			mat4 WORLD_FROM_LOCAL;			
		};
		static_assert(sizeof(Transform) == 16 * 4, "Transform is the expected size.");

		struct Light {
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Light) == 16 * 4, "Light is the expected size.");

		VkPipeline handle = VK_NULL_HANDLE;

		VkPipelineLayout layout = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} shadow_maps_pipeline;

	struct AVBDPipeline {
		VkDescriptorSetLayout set0_Rigids = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_UpdatedRigids = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_Manifolds = VK_NULL_HANDLE;
		VkDescriptorSetLayout set3_Colors = VK_NULL_HANDLE;
		VkDescriptorSetLayout set4_Counter = VK_NULL_HANDLE;

		struct Push {
			uint32_t rigidbody_count;
			int32_t init;
		};

		struct UpdatedRigid {
			vec4 positionLin; 
			vec4 positionAng; 
		};

		struct Rigid
		{
			float3 positionLin;
			float mass;

			quat positionAng;

			float3 initialLin;
			float friction;

			quat initialAng;

			float3 inertialLin;
			float radius;

			quat inertialAng;

			float3 velocityLin; 
			uint32_t isStatic;

			float3 velocityAng;
			enum class Shape
			{
				Box,
				Sphere
			} shape;

			float3 prevVelocityLin; 
			uint32_t listHead;

			float3 size; 
			float pad1_;

			float3 moment;
			float pad2_;
			
		};

		struct Force {
			uint32_t bodyA;
			uint32_t bodyB;
			int32_t next;
			uint32_t _pad0;
		};

		struct Contact {
			float3 rA;
			uint32_t feature;			
			float3 rB;
			uint32_t stick;

			float3 C0;
			float _pad3;

			float3 penalty;
			float _pad4;

			float3 lambda;
			float _pad5;
		};

		struct Manifold {
			Force force;          
			Contact contacts[8];
			
			struct {
				float3 row; float p0;
				float3 row1; float p1;
				float3 row2; float p2;
			} basis;   

			int numContacts;
			float friction;			
		};
		
		struct Color {
			uint32_t color;
		};

		struct Counter {
			int32_t counter;
		};

		VkPipeline handle = VK_NULL_HANDLE;
		VkPipeline broad_collision_handle = VK_NULL_HANDLE;
		VkPipeline precise_collision_handle = VK_NULL_HANDLE;
		VkPipeline graph_color_handle = VK_NULL_HANDLE;
		VkPipeline main_loop_init_handle = VK_NULL_HANDLE;
		VkPipeline main_loop_handle = VK_NULL_HANDLE;
		VkPipeline main_loop_copy_back_handle = VK_NULL_HANDLE;
		VkPipeline main_loop_dual_handle = VK_NULL_HANDLE;
		VkPipeline velocity_update_handle = VK_NULL_HANDLE;


		VkPipelineLayout layout = VK_NULL_HANDLE;

		

		void create(RTG&, VkRenderPass, uint32_t subpass);
		void destroy(RTG&);
	} avbd_pipeline;

	// command buffer for avbd pipeline
	struct AVBD {
		bool init = false;
		VkCommandBuffer command_buffer = VK_NULL_HANDLE;

		Helpers::AllocatedBuffer Rigidbodies_cpu;
		Helpers::AllocatedBuffer Rigidbodies;
		VkDescriptorSet Rigidbodies_descriptors = VK_NULL_HANDLE;

		Helpers::AllocatedBuffer UpdatedRigidbodies;
		VkDescriptorSet UpdatedRigidbodies_descriptors = VK_NULL_HANDLE;

		Helpers::AllocatedBuffer Manifolds;
		VkDescriptorSet Manifolds_descriptors = VK_NULL_HANDLE;

		Helpers::AllocatedBuffer Colors;
		VkDescriptorSet Colors_descriptors = VK_NULL_HANDLE;

		Helpers::AllocatedBuffer Counter;
		VkDescriptorSet Counter_descriptors = VK_NULL_HANDLE;

	} avbd;




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

		// Lights data
		Helpers::AllocatedBuffer Lights_src;
		Helpers::AllocatedBuffer Lights;
		VkDescriptorSet Lights_descriptors;

		// shadow map data		
		Helpers::AllocatedBuffer Shadow_map_transforms_src;
		Helpers::AllocatedBuffer Shadow_map_transforms;
		VkDescriptorSet Shadow_map_transforms_descriptors;

		bool ready_for_query = false;
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
	Helpers::AllocatedBuffer mesh_indices_buffer;
	struct MeshSlice {
		uint32_t first = 0;
		uint32_t count = 0;
	};
	std::unordered_map<std::string, MeshSlice> mesh_vertices;
	
	std::unordered_map<std::string, MeshSlice> mesh_indices;

	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;

	std::vector<Helpers::AllocatedImage> textures;
	std::vector<VkImageView> texture_views;
	std::unordered_map<std::string, uint32_t> texture_name_to_index;	

	std::unordered_map<S72::color, uint32_t> texture_color_to_index;
	std::unordered_map<float, uint32_t> texture_float_to_index;

	VkSampler texture_mipmap_sampler = VK_NULL_HANDLE;
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
		Free,
		Debug,
		
	} camera_mode = CameraMode::Scene;

	CameraMode previous_camera_mode = CameraMode::Scene;

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

	//used when camera_mode == CameraMode::Scene:
	struct SceneCamera {
		float eye_x = 0, eye_y = 0, eye_z = 0;
		float up_x = 0, up_y = 0, up_z = 0;
		float forward_x = 0, forward_y = 0, forward_z = 0;
		float fov = 60.0f * float(M_PI) / 180.0f;
		float near = 0.1f;
		float far = 10.0f;
		float aspect = 1.0f;
		bool ready = false;
		mat4 inverse;
	} scene_camera;

	std::vector<std::tuple<std::string, mat4, mat4>> delayed_culling_objects;

	//used when camera_mode == CameraMode::Debug:
	OrbitCamera debug_camera{
		.target_x = 0, .target_y = 0, .target_z = 0,
		.radius = 2.0f,
		.azimuth = 0.0f,
		.elevation = 0.25f * float(M_PI),
		.fov = 60.0f * float(M_PI) / 180.0f,
		.near = 0.1f,
		.far = 10000.0f
	};

	//computed from the current camera (as set by camera_mode) during update():
	mat4 CLIP_FROM_WORLD;

	std::vector<LinesPipeline::Vertex> lines_vertices;

	struct ObjectInstance {
		MeshSlice vertices;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;
		enum class Type {
			ALBEDO,
			ENV,
			MIRROR,
			PBR
		} texture_type;
		uint32_t normal_map = 1;
		uint32_t metalness_map = 0;
		uint32_t roughness_map = 0;
	};
	std::vector<ObjectInstance> object_instances;

	std::vector<ObjectsPipeline::Light> lights;
	std::vector<uint32_t> shadow_map_sizes;
		
	ObjectsPipeline::World world;

	// loading all mesh indices
	std::vector<Vertex> load_mesh_vertices();
	std::pair<std::vector<Vertex>, std::vector<uint32_t>> load_mesh_vertices_indexed();

	// loading all objects
	void load_objects();
	void load_objects(const S72::Node* root, const mat4& world_from_local, const mat4& world_from_local_normal);
	void render_mesh(const S72::Mesh& mesh, const mat4& world_from_local, const mat4& world_from_local_normal);
	void render_box(const mat4& world_from_local, float extent_x, float extent_y, float extent_z);
	void render_sphere(const mat4& world_from_local, float radius);
	// loading all textures
	void load_textures();

	// construct bounding boxes
	void construct_bounding_boxes(const std::vector<Vertex>& vertices);
	struct BoundingBox {
		float min_x, min_y, min_z;
		float max_x, max_y, max_z;
	};
	std::unordered_map<std::string, BoundingBox> mesh_bounding_boxes;

	bool is_mesh_in_frustum(const std::string& name, const BoundingBox& box, const mat4& WORLD_FROM_LOCAL);
	bool sat_intersect(const std::array<vec4, 8>& box_corners, const std::array<vec4, 8>& frustum_corners, const vec4 &axis);
	// draw the frustum lines
	void draw_frustum();

	// driver channel values
	struct DriverChannelValues {
		S72::vec3 translation = S72::vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f };
		S72::vec3 scale = S72::vec3{ .x = 1.0f, .y = 1.0f, .z = 1.0f };
		S72::quat rotation = S72::quat{ .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f };
	};

	void update_driver_channels(float dt);
	std::pair<uint32_t, uint32_t> find_time_interval(const std::vector<float>& times, float t);
	vec4 interpolate(const vec4& start, const vec4& end, float t, S72::Driver::Interpolation interpolation);

	bool constraintInit = false;
	void initializeJointConstraint();
	void update_physics(float dt);

	float playback_rate = 1.0f;
	
	class DriverChannelType {
	public:
		const static uint8_t Translation = 1;
		const static uint8_t Scale = 2;
		const static uint8_t Rotation = 4;
	};

	struct DriverValue {
		uint8_t type;
		S72::vec3 translation;
		S72::vec3 scale;
		S72::quat rotation;
	};
	std::unordered_map <std::string, DriverValue> driver_channel_values;

	struct PhysicsData {		
		Rigid* rigid;
		S72::RigidBody* s72Rigidbody;
	};

	std::unordered_map<std::string, PhysicsData> physics_data;



	S72::color srgb_to_linear(const S72::color& c);

	// profiling
	VkQueryPool query_pool = VK_NULL_HANDLE;

	double get_query_results(uint32_t workspace_index);

	double timestamp_period = 0;

	void rgbe_to_e5b9g9r9(unsigned char* rgbe);

	//get camera position
	vec4 get_current_camera_position();

	// env texture index
	uint32_t env_texture_index = UINT32_MAX;

	// lut texture index
	uint32_t lut_texture_index = UINT32_MAX;

	// lambertian texture index
	uint32_t lambertian_texture_index = UINT32_MAX;

	// max mipmap level
	uint32_t max_mipmap_level = 1;

	// render shadow maps
	void compute_shadow_maps(Workspace&);

	// shadow map resources
	struct ShadowMap {
		Helpers::AllocatedImage depth_image;
		VkImageView depth_image_view = VK_NULL_HANDLE;
		mat4 LIGHT_CLIP_FROM_WORLD;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		bool needs_update = false;
		uint32_t light_index = UINT32_MAX;
	};

	VkDescriptorPool shadow_map_descriptor_pool = VK_NULL_HANDLE;
	std::vector<ShadowMap> shadow_maps;
	VkDescriptorSet shadow_map_descriptor = VK_NULL_HANDLE;
	VkSampler shadow_map_sampler = VK_NULL_HANDLE;

	// shadow map renderpass
	VkRenderPass shadow_map_render_pass = VK_NULL_HANDLE;

	// the solver
	std::unique_ptr<Solver> solver;

	// update shadow maps
	void update_shadow_maps();	

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
