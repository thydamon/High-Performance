/**
 * 线程亲和性示例
 *
 * 来源：《Building Low Latency Applications with C++》第四章 4.1 小节
 * 目标：演示如何在 Linux 下用 pthread_setaffinity_np 把线程绑定到指定 CPU 核心，
 *      并对比绑定前后的调度稳定性。
 *
 * 编译：
 *   g++ -std=c++17 -O2 -pthread -o thread_affinity_demo thread_affinity_demo.cpp
 *
 * 运行：
 *   ./thread_affinity_demo
 *
 * 注意：
 *   - 只在 Linux/GNU 环境下有效（pthread_setaffinity_np 是 GNU 扩展）。
 *   - 建议用 isolcpus + nohz_full + rcu_nocbs 隔离核心后再跑关键线程，
 *     效果最明显。
 */

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <pthread.h>
#include <sched.h>
#include <cassert>

// 把当前线程绑定到指定的 CPU 核心
void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Failed to set affinity to core " << core_id
                  << ", error: " << rc << "\n";
    }
}

// 获取当前线程所在的 CPU 核心号
int get_current_core() {
    return sched_getcpu();
}

// 简单 benchmark：循环读取当前核心号，测量总耗时
std::chrono::microseconds::rep benchmark_core_query(int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        volatile int core = get_current_core();
        (void)core;
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int main() {
    const int iterations = 1'000'000;

    // ---------- 1. 单线程绑定示例 ----------
    std::cout << "=== 1. 单线程绑定示例 ===\n";
    std::thread worker([]() {
        pin_thread_to_core(2);  // 绑定到 CPU 核心 2
        std::cout << "Worker thread running on core " << get_current_core() << "\n";
    });
    worker.join();

    // ---------- 2. 多线程分别绑定到不同核心 ----------
    std::cout << "\n=== 2. 多线程分别绑定到不同核心 ===\n";
    const std::vector<int> target_cores = {0, 1, 2, 3};
    std::vector<std::thread> threads;
    threads.reserve(target_cores.size());

    for (int core : target_cores) {
        threads.emplace_back([core]() {
            pin_thread_to_core(core);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::cout << "Thread pinned to core " << core
                      << " actually runs on core " << get_current_core() << "\n";
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    // ---------- 3. 绑定前后的简单耗时对比 ----------
    std::cout << "\n=== 3. 绑定前后耗时对比 ===\n";
    std::cout << "不绑定核心: " << benchmark_core_query(iterations) << " us\n";

    pin_thread_to_core(2);
    std::cout << "绑定到核心 2: " << benchmark_core_query(iterations)
              << " us, current core: " << get_current_core() << "\n";

    return 0;
}
