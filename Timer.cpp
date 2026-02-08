#include "Timer.hpp"

Timer::Timer(std::function<void(double)> callback)
	:start_(std::chrono::high_resolution_clock::now()), callback_(callback)
{
}

Timer::~Timer() {
	auto duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_).count();
	callback_(duration);
}