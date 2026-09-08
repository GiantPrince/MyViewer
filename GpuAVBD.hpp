#pragma once
#include "RTG.hpp"
#include "solver.hpp"
#include <array>
#include <cstddef>

// GPU state is authoritative after construction. readback updates the CPU bodies
// for the renderer; contact history stays on the device for the solver's lifetime.
class GpuAVBD {
public:
    struct Body {
        float3 positionLin; float mass;
        quat positionAng;
        float3 initialLin; float friction;
        quat initialAng;
        float3 inertialLin; float radius;
        quat inertialAng;
        float3 velocityLin; uint32_t isStatic;
        float3 velocityAng; uint32_t shape;
        float3 prevVelocityLin; int32_t listHead;
        float3 size; uint32_t quietSteps;
        float3 moment; uint32_t sleeping;
    };
    struct Contact {
        float3 rA; uint32_t feature;
        float3 rB; uint32_t stick;
        float3 C0; float pad0;
        float3 penalty; float pad1;
        float3 lambda; float pad2;
    };
    struct alignas(16) Manifold {
        uint32_t bodyA, bodyB; int32_t nextA, nextB;
        Contact contacts[8];
        float basis[12];
        uint32_t numContacts; float friction;
        uint32_t padding[2];
    };
    struct Stats {
        uint32_t errors, staticCount, activeCount, retainedCount, sleepingCount, awakeCount;
        uint32_t colorCounts[32];
        uint32_t dispatches[102];
    };
    static_assert(sizeof(Body) == 176);
    static_assert(sizeof(Contact) == 80);
    static_assert(sizeof(Manifold) == 720);
    static_assert(offsetof(Manifold, numContacts) == 704);

    GpuAVBD(RTG&, Solver&, uint32_t slotsPerBody = 8, bool sleeping = true);
    ~GpuAVBD();
    GpuAVBD(const GpuAVBD&) = delete;
    GpuAVBD& operator=(const GpuAVBD&) = delete;
    void step();
    const Stats& stats() const { return lastStats; }
    std::vector<Manifold> readContacts(); // Small-scene validation only.
    double gpuMilliseconds() const { return gpuMs; }
    std::array<double, 6> stageMilliseconds{};
private:
    RTG& rtg;
    Solver& solver;
    std::vector<Rigid*> bodies;
    struct Push {
        uint32_t count; int32_t color;
        uint32_t hashSize, slots;
        float cellSize, dt, gravity, betaLin, alpha, gamma;
        uint32_t sleepEnabled;
    } push{};
    std::array<Helpers::AllocatedBuffer, 5> buffers;
    Helpers::AllocatedBuffer readback;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 5> sets{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 10> pipelines{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    Stats lastStats{};
    double gpuMs = 0;
    float timestampPeriod = 0;
    void record();
};
