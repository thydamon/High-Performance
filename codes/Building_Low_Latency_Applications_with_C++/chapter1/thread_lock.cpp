#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <algorithm>

std::mutex mtx;
volatile int counter = 0;

void woker(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter;
    }
}

int main() {
    using namespace std::chrono;

    const int iterations = 100'000;
    std::vector<long long> times;
    times.reserve(iterations);

    // 单线程无锁测量
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        ++counter;
    }
    auto end = high_resolution_clock::now();
    std::cout << "单线程无锁总耗时："
              << duration_cast<microseconds>(end - start).count() 
              << " us\n" << std::endl;

    // 多线程加锁测量
    counter = 0; // 重置计数器
    auto start2 = high_resolution_clock::now();
    std::thread t1(woker, iterations);
    std::thread t2(woker, iterations);
    t1.join();
    t2.join();
    auto end2 = high_resolution_clock::now();
    std::cout << "多线程加锁总耗时："
              << duration_cast<microseconds>(end2 - start2).count() 
              << " us\n" << std::endl;

    return 0;
}