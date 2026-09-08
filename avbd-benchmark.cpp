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
        uint32_t count = 1, frames = 600, layers = 1, iterations = 10, substeps = 1;
        float height = 3.0f, offset = 0, tilt = 0;
        bool compare = false, debug = false, sleeping = true, wake = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--wake") wake = true;
            else if (arg == "--compare") compare = true;
            else if (arg == "--no-sleep") sleeping = false;
            else if (arg == "--debug") debug = true;
            else if (i+1 < argc && arg == "--cubes") count = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--frames") frames = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--layers") layers = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--substeps") substeps = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--iterations") iterations = uint32_t(std::stoul(argv[++i]));
            else if (i+1 < argc && arg == "--tilt") tilt = std::stof(argv[++i]);
            else if (i+1 < argc && arg == "--offset") offset = std::stof(argv[++i]);
            else if (i+1 < argc && arg == "--height") height = std::stof(argv[++i]);
            else throw std::runtime_error("Usage: avbd-benchmark [--cubes N] [--frames N>=600] [--layers N] [--height Y] [--offset XZ] [--tilt radians] [--iterations N] [--substeps N] [--compare] [--wake] [--no-sleep] [--debug]");
        }
        if (!count || frames < 600 || !layers || layers > count || !iterations || !substeps) throw std::runtime_error("Invalid benchmark dimensions");
        if (wake) { count = 2; layers = 2; }
        if (compare && count > 1000) throw std::runtime_error("CPU comparison is limited to 1000 cubes (CPU reference broad phase is quadratic).");
        RTG::Configuration config;
        config.headless = true; config.is_cube_utility = true; config.debug = debug;
        config.surface_extent = {16,16};
        RTG rtg(config);
        Solver gpuState, cpu;
        uint32_t columns = (count + layers - 1) / layers;
        uint32_t side = uint32_t(std::ceil(std::sqrt(double(columns))));
        auto populate = [&](Solver& s) {
            new Rigid(&s, {wake ? 50.0f : float(side)*2+4+std::abs(offset)*2,1,wake ? 50.0f : float(side)*2+4+std::abs(offset)*2},1,0.5f,{0,-0.5f,0},{0,0,0,1},{0,0,0},Rigid::Shape::Box,true);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t column = i / layers, layer = i % layers;
                float x = (float(column % side)-float(side-1)*0.5f)*1.5f;
                float z = (float(column / side)-float(side-1)*0.5f)*1.5f;
                new Rigid(&s,{1,1,1},1,wake ? 0.0f : 0.5f,{wake && layer == 1 ? -12.0f : x+offset,wake ? (layer == 0 ? 3.0f : 0.49f) : height+float(layer)*1.0f,z+offset},{0,0,std::sin(tilt*0.5f),std::cos(tilt*0.5f)},{wake && layer == 1 ? 3.0f : 0.0f,0,0});
            }
        };
        populate(gpuState); if (compare) populate(cpu);
        gpuState.iterations = int(iterations);
        gpuState.dt /= float(substeps);
        GpuAVBD gpu(rtg,gpuState,8,sleeping);
        std::vector<double> wall, device;
        double maxPositionError = 0, finalPositionError = 0, cpuMinBottom = 1e9, minBottom = 1e9, minLateBottom = 1e9, maxLateSpeed = 0;
        uint64_t retained = 0;
        bool sawSleep = false, sawWake = false;
        std::ostringstream quiet;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto start = std::chrono::steady_clock::now();
            for (uint32_t substep = 0; substep < substeps; ++substep) gpu.step();
            if (gpu.stats().sleepingCount) sawSleep = true;
            if (sawSleep && !gpu.stats().sleepingCount) sawWake = true;
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
                    if (!std::isfinite(bottom) || !std::isfinite(length(b->velocityLin))) throw std::runtime_error("Nonfinite benchmark state");
                    if (frame > frames*3/4) {
                        maxLateSpeed = std::max(maxLateSpeed,double(length(b->velocityLin)));
                        minLateBottom = std::min(minLateBottom,bottom);
                    }
                }
                if (compare) {
                    if (!ref->isStatic) {
                        auto x = rotate(ref->positionAng,{1,0,0}); auto y = rotate(ref->positionAng,{0,1,0}); auto z = rotate(ref->positionAng,{0,0,1});
                        cpuMinBottom = std::min(cpuMinBottom,double(ref->positionLin.y)-0.5*(std::abs(x.y)+std::abs(y.y)+std::abs(z.y)));
                    }
                    if (frame + 1 == frames) finalPositionError = std::max(finalPositionError,double(length(b->positionLin-ref->positionLin)));
                    maxPositionError = std::max(maxPositionError,double(length(b->positionLin-ref->positionLin))); ref = ref->next; }
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
                  << " min_bottom=" << minBottom << " late_min_bottom=" << minLateBottom << " late_max_speed=" << maxLateSpeed
                  << " slept=" << sawSleep << " woke=" << sawWake << " retained_contacts=" << retained << '\n';
        if (compare) std::cout << "max_cpu_position_error=" << maxPositionError << " final_cpu_position_error=" << finalPositionError << " cpu_min_bottom=" << cpuMinBottom << '\n';
        const double impactLimit = compare ? std::min(-0.05,cpuMinBottom-0.005) : -0.05;
        if ((wake && (!sawSleep || !sawWake)) || retained == 0 || minBottom < impactLimit || minLateBottom < -0.02 || (!wake && maxLateSpeed > 0.01) || (compare && finalPositionError > 0.05) || (compare && count == 1 && maxPositionError > 0.001)) return 2;
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
