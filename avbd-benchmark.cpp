#include "GpuAVBD.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

// No renderer or scene assets: measures the actual Vulkan solver on this device.
int main(int argc, char** argv) {
    try {
        uint32_t count = 1, frames = 600, layers = 1;
        float height = 3.0f;
        bool compare = false, debug = false, sleeping = true;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--compare") compare = true;
            else if (arg == "--no-sleep") sleeping = false;
            else if (arg == "--debug") debug = true;
            else if (i+1 < argc && arg == "--cubes") count = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--frames") frames = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--layers") layers = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--height") height = std::stof(argv[++i]);
            else throw std::runtime_error("Usage: avbd-benchmark [--cubes N] [--frames N] [--layers N] [--compare] [--debug]");
        }
        if (!count || !frames || !layers || layers > count) throw std::runtime_error("Invalid benchmark dimensions");
        if (compare && count > 1000) throw std::runtime_error("CPU comparison is limited to 1000 cubes (CPU reference broad phase is quadratic).");
        RTG::Configuration config;
        config.headless = true; config.is_cube_utility = true; config.debug = debug;
        config.surface_extent = {16,16};
        RTG rtg(config);
        Solver gpuState, cpu;
        uint32_t columns = (count + layers - 1) / layers;
        uint32_t side = uint32_t(std::ceil(std::sqrt(double(columns))));
        auto populate = [&](Solver& s) {
            new Rigid(&s, {float(side)*2+4,1,float(side)*2+4},1,0.5f,{0,-0.5f,0},{0,0,0,1},{0,0,0},Rigid::Shape::Box,true);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t column = i / layers, layer = i % layers;
                float x = (float(column % side)-float(side-1)*0.5f)*1.5f;
                float z = (float(column / side)-float(side-1)*0.5f)*1.5f;
                new Rigid(&s,{1,1,1},1,0.5f,{x,height+float(layer)*1.0f,z},{0,0,0,1});
            }
        };
        populate(gpuState); if (compare) populate(cpu);
        GpuAVBD gpu(rtg,gpuState,8,sleeping);
        std::vector<double> wall, device;
        double maxPositionError = 0, minBottom = 1e9, maxLateSpeed = 0;
        uint64_t retained = 0;
        std::ostringstream quiet;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto start = std::chrono::steady_clock::now();
            gpu.step();
            auto end = std::chrono::steady_clock::now();
            if (frame >= std::min(60u,frames/2)) {
                wall.push_back(std::chrono::duration<double,std::milli>(end-start).count());
                device.push_back(gpu.gpuMilliseconds());
            }
            retained += gpu.stats().retainedCount;
            if (compare) {
                auto* old = std::cout.rdbuf(quiet.rdbuf()); cpu.step(); std::cout.rdbuf(old); quiet.str("");
            }
            Rigid* ref = cpu.bodies;
            for (Rigid* b = gpuState.bodies; b; b = b->next) {
                if (!b->isStatic) {
                    float3 axisX = rotate(b->positionAng,{1,0,0});
                    float3 axisY = rotate(b->positionAng,{0,1,0});
                    float3 axisZ = rotate(b->positionAng,{0,0,1});
                    double bottom = b->positionLin.y - 0.5*(std::abs(axisX.y)+std::abs(axisY.y)+std::abs(axisZ.y));
                    minBottom = std::min(minBottom,bottom);
                    if (frame > frames*3/4) maxLateSpeed = std::max(maxLateSpeed,double(length(b->velocityLin)));
                }
                if (compare) { maxPositionError = std::max(maxPositionError,double(length(b->positionLin-ref->positionLin))); ref = ref->next; }
            }
            if (frame % 60 == 0) {
                std::cerr << "frame=" << frame << " gpu_ms=" << gpu.gpuMilliseconds() << " pairs=" << gpu.stats().activeCount << " retained=" << gpu.stats().retainedCount << " sleeping=" << gpu.stats().sleepingCount << " stages=";
                for (auto ms : gpu.stageMilliseconds) std::cerr << ms << ',';
                std::cerr << '\n';
            }
        }
        auto median = [](std::vector<double> v) { std::sort(v.begin(),v.end()); return v[v.size()/2]; };
        std::cout << "cubes=" << count << " frames=" << frames << " layers=" << layers
                  << " median_step_ms=" << median(wall) << " median_gpu_ms=" << median(device)
                  << " min_bottom=" << minBottom << " late_max_speed=" << maxLateSpeed
                  << " retained_contacts=" << retained << " max_cpu_position_error=" << maxPositionError << '\n';
        if (retained == 0 || minBottom < -0.05 || (compare && count == 1 && maxPositionError > 0.001)) return 2;
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
