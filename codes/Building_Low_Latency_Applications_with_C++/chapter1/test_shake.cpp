#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>

int main() {
    using namespace std::chrono;

    const int N = 1'000'000;
    std::vector<long long> times;
    times.reserve(N);

    volatile int sum = 0;

    for (int run = 0; run < N; ++run) {
        auto start = high_resolution_clock::now();
        sum += run; // Simulate some work
        auto end = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(end - start).count());
    }

    std::sort(times.begin(), times.end());

    std::cout << "p50: " << times[N * 0.5] << " ns\n";
    std::cout << "p90: " << times[N * 0.99] << " ns\n";
    std::cout << "p999: " << times[N * 0.999] << " ns\n";
    std::cout << "max: " << times.back() << " ns\n";
}