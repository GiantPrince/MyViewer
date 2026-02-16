
#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>



struct Vertex {
	struct { float x, y, z; } Position;
	struct { float x, y, z; } Normal;
	struct { float x, y, z, w; } Tangent;
	struct { float s, t; } TexCoord;

	static const VkPipelineVertexInputStateCreateInfo array_input_state;

	bool operator==(Vertex const& other) const {
		return Position.x == other.Position.x && Position.y == other.Position.y && Position.z == other.Position.z
			&& Normal.x == other.Normal.x && Normal.y == other.Normal.y && Normal.z == other.Normal.z
			&& Tangent.x == other.Tangent.x && Tangent.y == other.Tangent.y && Tangent.z == other.Tangent.z
			&& TexCoord.s == other.TexCoord.s && TexCoord.t == other.TexCoord.t;
	}
};

static_assert(sizeof(Vertex) == 3 * 4 + 3 * 4 + 4 * 4 + 2 * 4, "Vertex is packed.");

namespace std {
    template<>
    struct hash<Vertex> {
        size_t operator()(Vertex const& v) const {

            size_t h = 0;

            auto add = [&h](auto const& val) {
                std::hash<float> fhasher;
                h ^= fhasher(val) + 0x9e3779a9 + (h << 5) + (h >> 2);
                };

            add(v.Position.x);
            add(v.Position.y);
            add(v.Position.z);

            add(v.Normal.x);
            add(v.Normal.y);
            add(v.Normal.z);

            add(v.Tangent.x);
            add(v.Tangent.y);
            add(v.Tangent.z);
            add(v.Tangent.w);

            add(v.TexCoord.s);
            add(v.TexCoord.t);

            return h;
        }
    };
}