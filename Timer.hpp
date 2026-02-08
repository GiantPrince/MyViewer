#include <chrono>
#include <functional>

class Timer {
public:
	Timer(std::function<void(double)> callback);
	~Timer();

private:
	std::chrono::high_resolution_clock::time_point start_;
	std::function<void(double)> callback_;
};