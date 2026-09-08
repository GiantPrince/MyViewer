#include "GpuAVBD.hpp"
#include "VK.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
const uint32_t broad[] =
#include "spv/broad-collision.comp.inl"
;
const uint32_t aligned[] =
#include "spv/aligned-collision.comp.inl"
;
const uint32_t narrow[] =
#include "spv/precise-collision.comp.inl"
;
const uint32_t linkContacts[] =
#include "spv/contact-link.comp.inl"
;
const uint32_t color[] =
#include "spv/graph-color.comp.inl"
;
const uint32_t init[] =
#include "spv/main-loop-init.comp.inl"
;
const uint32_t primal[] =
#include "spv/main-loop.comp.inl"
;
const uint32_t dual[] =
#include "spv/main-loop-dual.comp.inl"
;
const uint32_t velocity[] =
#include "spv/velocity-update.comp.inl"
;
const uint32_t pairs[] =
#include "spv/pair-discovery.comp.inl"
;
}

GpuAVBD::GpuAVBD(RTG& r, Solver& s, uint32_t slots, bool sleeping) : rtg(r), solver(s) {
    if (s.forces) throw std::runtime_error("GPU AVBD currently supports contacts only; use --physics cpu for joint scenes.");
    if (slots == 0 || slots > 32) throw std::runtime_error("GPU contact slots must be in [1,32].");
    std::vector<Body> upload;
    for (Rigid* b = solver.bodies; b; b = b->next) {
        bodies.push_back(b);
        Body v{};
        v.positionLin = b->positionLin; v.positionAng = b->positionAng;
        v.mass = b->mass; v.friction = b->friction; v.radius = b->radius;
        v.velocityLin = b->velocityLin; v.velocityAng = b->velocityAng;
        v.prevVelocityLin = b->prevVelocityLin;
        v.isStatic = b->isStatic; v.shape = uint32_t(b->shape);
        v.size = b->size; v.moment = b->moment; v.listHead = -1;
        upload.push_back(v);
        if (!b->isStatic) push.cellSize = std::max(push.cellSize, 2 * b->radius);
    }
    if (upload.empty()) throw std::runtime_error("Cannot create GPU solver without bodies.");
    push.count = uint32_t(upload.size()); push.slots = slots;
    push.hashSize = 1;
    while (push.hashSize < push.count * 2) push.hashSize *= 2;
    push.cellSize = std::max(push.cellSize, 0.001f);
    push.dt = s.dt; push.gravity = s.gravity; push.betaLin = s.betaLin;
    push.alpha = s.alpha; push.gamma = s.gamma;
    push.sleepEnabled = sleeping;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(rtg.physical_device, &properties);
    timestampPeriod = properties.limits.timestampPeriod;
    const std::array<VkDeviceSize,5> sizes{
        upload.size() * sizeof(Body),
        (VkDeviceSize(push.hashSize) + push.count * (68ull + slots)) * sizeof(int32_t),
        VkDeviceSize(push.count) * slots * sizeof(Manifold),
        VkDeviceSize(push.count) * 33 * sizeof(int32_t), sizeof(Stats)
    };
    for (auto bytes : sizes) if (bytes > properties.limits.maxStorageBufferRange)
        throw std::runtime_error("GPU AVBD storage exceeds maxStorageBufferRange; reduce contact slots or body count.");
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1; setInfo.pBindings = &binding;
    VK(vkCreateDescriptorSetLayout(rtg.device, &setInfo, nullptr, &setLayout));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 5; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &poolSize;
    VK(vkCreateDescriptorPool(rtg.device, &poolInfo, nullptr, &descriptorPool));
    std::array<VkDescriptorSetLayout,5> layouts; layouts.fill(setLayout);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool; alloc.descriptorSetCount = 5; alloc.pSetLayouts = layouts.data();
    VK(vkAllocateDescriptorSets(rtg.device, &alloc, sets.data()));
    for (size_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = rtg.helpers.create_buffer(sizes[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDescriptorBufferInfo info{buffers[i].handle, 0, sizes[i]};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sets[i]; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(rtg.device, 1, &write, 0, nullptr);
    }
    VkMemoryPropertyFlags readbackFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < rtg.helpers.memory_properties.memoryTypeCount; ++i) {
        auto flags = rtg.helpers.memory_properties.memoryTypes[i].propertyFlags;
        if ((flags & readbackFlags) == readbackFlags && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
            readbackFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT; break;
        }
    }
    readback = rtg.helpers.create_buffer(sizes[0] + sizeof(Stats), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        readbackFlags, Helpers::Mapped);
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 5; pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1; pipelineLayoutInfo.pPushConstantRanges = &range;
    VK(vkCreatePipelineLayout(rtg.device, &pipelineLayoutInfo, nullptr, &layout));
    const uint32_t* codes[] = {broad,narrow,linkContacts,color,init,primal,dual,velocity,pairs,aligned};
    const size_t bytes[] = {sizeof(broad),sizeof(narrow),sizeof(linkContacts),sizeof(color),sizeof(init),sizeof(primal),sizeof(dual),sizeof(velocity),sizeof(pairs),sizeof(aligned)};
    for (size_t i = 0; i < pipelines.size(); ++i) {
        VkShaderModule module = rtg.helpers.create_shader_module(codes[i],bytes[i]);
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; info.stage.module = module; info.stage.pName = "main";
        info.layout = layout;
        VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[i]));
        vkDestroyShaderModule(rtg.device,module,nullptr);
    }
    VkCommandPoolCreateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandInfo.queueFamilyIndex = rtg.graphics_queue_family.value();
    commandInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK(vkCreateCommandPool(rtg.device,&commandInfo,nullptr,&commandPool));
    VkCommandBufferAllocateInfo commandAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAlloc.commandPool = commandPool; commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandAlloc.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(rtg.device,&commandAlloc,&command));
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK(vkCreateFence(rtg.device,&fenceInfo,nullptr,&fence));
    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP; queryInfo.queryCount = 7;
    VK(vkCreateQueryPool(rtg.device,&queryInfo,nullptr,&queries));
    rtg.helpers.transfer_to_buffer(upload.data(),sizes[0],buffers[0]);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(command,&begin));
    vkCmdFillBuffer(command,buffers[2].handle,0,VK_WHOLE_SIZE,0);
    VK(vkEndCommandBuffer(command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    VK(vkQueueSubmit(rtg.graphics_queue,1,&submit,fence));
    VK(vkWaitForFences(rtg.device,1,&fence,VK_TRUE,UINT64_MAX));
    VK(vkResetFences(rtg.device,1,&fence));
    VK(vkResetCommandBuffer(command,0));
    record();
}

void GpuAVBD::record() {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(command,&begin));
    auto barrier = [&]() {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
    };
    barrier();
    vkCmdResetQueryPool(command,queries,0,7);
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
    Stats reset{};
    for (int i = 0; i < 34; ++i) { reset.dispatches[3*i+1] = 1; reset.dispatches[3*i+2] = 1; }
    vkCmdUpdateBuffer(command,buffers[4].handle,0,sizeof(reset),&reset);
    vkCmdFillBuffer(command,buffers[1].handle,0,push.hashSize * sizeof(int32_t),0xffffffffu);
    barrier();
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,5,sets.data(),0,nullptr);
    auto constants = [&]() { vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push); };
    auto stage = [&](size_t index, uint32_t groups) {
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[index]);
        vkCmdDispatch(command,groups,1,1); barrier();
    };
    auto indirect = [&](size_t index, uint32_t dispatch) {
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[index]);
        vkCmdDispatchIndirect(command,buffers[4].handle,offsetof(Stats,dispatches) + dispatch * 12);
        barrier();
    };
    constants();
    stage(0,(push.count+127)/128);
    stage(8,(push.count+127)/128);
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
    stage(9,(push.count+63)/64);
    stage(1,(push.count+63)/64);
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,2);
    indirect(2,32);
    stage(4,(push.count+127)/128);
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,3);
    for (int round = 0; round < 32; ++round) { push.color = round; constants(); indirect(3,33); }
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,4);
    for (int iteration = 0; iteration < solver.iterations; ++iteration) {
        for (int c = 0; c < 32; ++c) { push.color = c; constants(); indirect(5,uint32_t(c)); }
        indirect(6,32);
    }
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,5);
    stage(7,(push.count+127)/128);
    vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,6);
    VkBufferCopy copy{0,0,buffers[0].size};
    vkCmdCopyBuffer(command,buffers[0].handle,readback.handle,1,&copy);
    copy = {0,buffers[0].size,sizeof(Stats)};
    vkCmdCopyBuffer(command,buffers[4].handle,readback.handle,1,&copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    VK(vkEndCommandBuffer(command));
}

void GpuAVBD::step() {
    if (solver.num_bodies != int(bodies.size())) throw std::runtime_error("GPU body topology changed; recreate solver.");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    VK(vkQueueSubmit(rtg.graphics_queue,1,&submit,fence));
    VK(vkWaitForFences(rtg.device,1,&fence,VK_TRUE,UINT64_MAX));
    VK(vkResetFences(rtg.device,1,&fence));
    const auto* data = static_cast<const Body*>(readback.allocation.data());
    std::memcpy(&lastStats,reinterpret_cast<const char*>(data)+buffers[0].size,sizeof(lastStats));
    if (lastStats.errors) throw std::runtime_error("GPU AVBD failed: error bits " + std::to_string(lastStats.errors) +
        " (1=contact capacity, 2=color capacity, 4=nonfinite state, 8=candidate capacity). No invalid frame was published.");
    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = *bodies[i]; const auto& v = data[i];
        b.positionLin = v.positionLin; b.positionAng = v.positionAng;
        b.initialLin = v.initialLin; b.initialAng = v.initialAng;
        b.inertialLin = v.inertialLin; b.inertialAng = v.inertialAng;
        b.velocityLin = v.velocityLin; b.velocityAng = v.velocityAng; b.prevVelocityLin = v.prevVelocityLin;
    }
    uint64_t times[7]{};
    VK(vkGetQueryPoolResults(rtg.device,queries,0,7,sizeof(times),times,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    gpuMs = double(times[6]-times[0]) * timestampPeriod / 1e6;
    for (size_t i = 0; i < 6; ++i) stageMilliseconds[i] = double(times[i+1]-times[i]) * timestampPeriod / 1e6;
}

std::vector<GpuAVBD::Manifold> GpuAVBD::readContacts() {
    std::vector<unsigned char> bytes;
    rtg.helpers.transfer_buffer_to_vector(buffers[2],bytes);
    std::vector<Manifold> result(bytes.size()/sizeof(Manifold));
    std::memcpy(result.data(),bytes.data(),bytes.size());
    return result;
}

GpuAVBD::~GpuAVBD() {
    if (commandPool) vkDestroyCommandPool(rtg.device,commandPool,nullptr);
    if (fence) vkDestroyFence(rtg.device,fence,nullptr);
    if (queries) vkDestroyQueryPool(rtg.device,queries,nullptr);
    for (auto pipeline : pipelines) if (pipeline) vkDestroyPipeline(rtg.device,pipeline,nullptr);
    if (layout) vkDestroyPipelineLayout(rtg.device,layout,nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(rtg.device,descriptorPool,nullptr);
    if (setLayout) vkDestroyDescriptorSetLayout(rtg.device,setLayout,nullptr);
    for (auto& buffer : buffers) if (buffer.handle) rtg.helpers.destroy_buffer(std::move(buffer));
    if (readback.handle) rtg.helpers.destroy_buffer(std::move(readback));
}
