/**
 * 线程亲和性尾延迟 benchmark
 *
 * 目标：说明线程亲和性主要降低的是尾延迟（p99/p999）和抖动，
 *       而不是简单的平均耗时。
 *
 * 编译：
 *   g++ -std=c++17 -O2 -pthread -o thread_affinity_latency_bench thread_affinity_latency_bench.cpp
 *
 * 运行：
 *   ./thread_affinity_latency_bench
 */

// 标准输入输出，用于打印 benchmark 结果
#include <iostream>
// C++11 线程库，用来创建前台线程和后台负载线程
#include <thread>
// 动态数组，存线程对象和采样数据
#include <vector>
// 排序算法，计算分位点时需要先排序
#include <algorithm>
// 原子变量，用来安全地通知后台线程停止
#include <atomic>
// 高精度时钟，用于 TSC 校准时获取真实时间
#include <chrono>
// POSIX 线程 API，提供 pthread_setaffinity_np 设置亲和性
#include <pthread.h>
// 提供 cpu_set_t、CPU_ZERO、CPU_SET、sched_getcpu
#include <sched.h>
// 固定宽度整数类型，如 uint64_t
#include <cstdint>

// 绑定当前线程到指定的 CPU 核心
void pin_to_core(int core_id) {
    // cpu_set_t 是一个位图，每一位代表一个 CPU 核心
    cpu_set_t cpuset;
    // 清空位图，表示暂时不允许任何核心
    CPU_ZERO(&cpuset);
    // 把 core_id 对应的位置为 1，表示允许该核心
    CPU_SET(core_id, &cpuset);

    // pthread_self() 获取当前线程的 pthread 标识符
    // pthread_setaffinity_np 设置当前线程只能在 cpuset 指定的核心上运行
    // _np 表示 non-portable，是 GNU 扩展，Linux 专用
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        // 核心号越界等情况会返回非零错误码
        std::cerr << "Failed to pin to core " << core_id << ", rc=" << rc << "\n";
    }
}

// 简单的 CPU burn，用来模拟后台负载，让系统有调度压力
void burn_cpu(std::atomic_bool* stop) {
    // volatile 阻止编译器把这段计算优化掉，否则空循环可能被删除
    volatile uint64_t sink = 0;

    // 当外部把 stop 置为 true 时退出循环
    // memory_order_relaxed 只要求原子性，不要求顺序同步，开销最小
    while (!stop->load(std::memory_order_relaxed)) {
        // 每次内层循环做一点整数累加，持续占用 CPU
        for (int i = 0; i < 1000; ++i) {
            sink += static_cast<uint64_t>(i);
        }
    }

    // 消除“sink 未使用”的编译器警告，不影响逻辑
    (void)sink;
}

// 模拟一次关键路径要做的小量计算
// inline 建议编译器内联展开，减少函数调用开销
inline uint64_t do_work() {
    // volatile 防止编译器把整个循环优化掉
    volatile uint64_t sink = 0;
    for (int i = 0; i < 1000; ++i) {
        sink += static_cast<uint64_t>(i);
    }
    return sink;
}

// 读取 CPU 的时间戳计数器（Time Stamp Counter）
// TSC 比 std::chrono::now() 精度更高、调用开销更低
static inline uint64_t rdtsc() {
    // rdtsc 指令把 64 位时间戳的低 32 位放到 eax，高 32 位放到 edx
    uint32_t lo, hi;
    // __volatile__ 防止编译器重排或优化掉这条汇编
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    // 把高 32 位和低 32 位拼接成 64 位结果
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// 一次 benchmark 的延迟统计结果，所有字段单位都是纳秒
struct LatencyStats {
    uint64_t min_ns;   // 最小延迟
    uint64_t p50_ns;   // 中位数
    uint64_t p99_ns;   // 99 分位
    uint64_t p999_ns;  // 99.9 分位
    uint64_t max_ns;   // 最大延迟
    uint64_t avg_ns;   // 平均延迟
};

// 全局变量：每纳秒对应的 TSC 周期数，由 calibrate_tsc() 校准得到
static double tsc_cycles_per_ns = 0.0;

// 校准 TSC：把 TSC 周期换算成真实时间（纳秒）
void calibrate_tsc() {
    // 记录校准开始时的 TSC 值
    const uint64_t cycles_start = rdtsc();
    // 记录校准开始时的真实时间
    auto t_start = std::chrono::high_resolution_clock::now();

    // 让线程 sleep 100 毫秒，作为校准的时间基准
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 记录校准结束时的 TSC 值
    const uint64_t cycles_end = rdtsc();
    // 记录校准结束时的真实时间
    auto t_end = std::chrono::high_resolution_clock::now();

    // 计算这 100ms 对应的真实纳秒数
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();
    // 计算这 100ms 对应的 TSC 周期数
    uint64_t cycles = cycles_end - cycles_start;

    // 每纳秒多少 TSC 周期，后面用 cycles / tsc_cycles_per_ns 得到纳秒
    tsc_cycles_per_ns = static_cast<double>(cycles) / static_cast<double>(ns);
    std::cout << "TSC calibrated: " << tsc_cycles_per_ns << " cycles/ns\n\n";
}

// 核心测量函数：在指定绑定策略和后台负载下，测量前台线程的延迟分布
//
// foreground_core:   前台线程计划绑定的核心号
// pin_foreground:    是否真正给前台线程设置亲和性
// background_cores:  后台负载线程分别绑定到哪些核心
// iterations:        前台线程采样次数
LatencyStats measure_latency(int foreground_core,
                             bool pin_foreground,
                             const std::vector<int>& background_cores,
                             int iterations) {
    // 原子开关，控制后台线程何时停止
    std::atomic_bool stop{false};

    // 保存后台负载线程
    std::vector<std::thread> workers;
    workers.reserve(background_cores.size());

    // 启动后台负载线程
    for (int core : background_cores) {
        // 每个后台线程先绑定到指定核心，然后进入 burn_cpu 无限循环
        workers.emplace_back([core, &stop]() {
            pin_to_core(core);
            burn_cpu(&stop);
        });
    }

    // 给后台线程 50ms 时间，确保它们真的跑起来，再开始测量
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 保存每一次 do_work() 的耗时样本，单位是 TSC 周期
    std::vector<uint64_t> samples;
    samples.reserve(iterations);

    // 创建前台测量线程
    std::thread foreground([&]() {
        // 如果调用方要求绑定，就把前台线程绑定到指定核心
        if (pin_foreground) {
            pin_to_core(foreground_core);
        }

        // warmup：先跑 10000 次，让缓存、分支预测、CPU 频率进入稳定状态
        for (int i = 0; i < 10000; ++i) {
            do_work();
        }

        // prev 记录上一次采样点的 TSC
        uint64_t prev = rdtsc();
        for (int i = 0; i < iterations; ++i) {
            // 执行一次关键路径操作
            (void)do_work();

            // now 记录本次操作结束后的 TSC
            uint64_t now = rdtsc();

            // 两次 TSC 差值就是这一次操作的耗时（含 do_work 和 rdtsc 本身开销）
            samples.push_back(now - prev);

            // 把当前时间作为下一次的上一次时间
            prev = now;
        }
    });

    // 等待前台线程跑完所有采样
    foreground.join();

    // 通知后台线程停止
    stop.store(true, std::memory_order_relaxed);
    // 等待所有后台线程退出
    for (auto& t : workers) {
        t.join();
    }

    // 排序后才能计算分位点
    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();

    // lambda：给定分位比例 p（0~1），返回对应位置的样本值
    auto percentile = [&samples, n](double p) {
        size_t idx = static_cast<size_t>((n - 1) * p);
        return samples[idx];
    };

    // 计算总周期数，用于求平均
    uint64_t sum = 0;
    for (uint64_t v : samples) sum += v;

    // 把 TSC 周期换算成纳秒，填入统计结构体
    LatencyStats s;
    s.min_ns = static_cast<uint64_t>(samples.front() / tsc_cycles_per_ns);
    s.p50_ns = static_cast<uint64_t>(percentile(0.50) / tsc_cycles_per_ns);
    s.p99_ns = static_cast<uint64_t>(percentile(0.99) / tsc_cycles_per_ns);
    s.p999_ns = static_cast<uint64_t>(percentile(0.999) / tsc_cycles_per_ns);
    s.max_ns = static_cast<uint64_t>(samples.back() / tsc_cycles_per_ns);
    s.avg_ns = static_cast<uint64_t>((sum / n) / tsc_cycles_per_ns);
    return s;
}

// 打印一组延迟统计结果
void print_stats(const char* label, const LatencyStats& s) {
    std::cout << label << ":\n"
              << "  min=" << s.min_ns << " ns\n"
              << "  avg=" << s.avg_ns << " ns\n"
              << "  p50=" << s.p50_ns << " ns\n"
              << "  p99=" << s.p99_ns << " ns\n"
              << "  p999=" << s.p999_ns << " ns\n"
              << "  max=" << s.max_ns << " ns\n\n";
}

int main() {
    // 第一步：校准 TSC，否则后面无法把周期换算成纳秒
    calibrate_tsc();

    // 总共采样 100 万次，样本量足够大，统计结果比较稳定
    const int iterations = 1'000'000;
    // 前台线程默认绑定到核心 2
    const int foreground_core = 2;

    // 获取当前机器的逻辑核心数
    const unsigned int ncores = std::thread::hardware_concurrency();
    std::cout << "Detected cores: " << ncores << "\n\n";

    // 这个 benchmark 的设计需要至少 4 个核心：
    // 核心 0、1 跑背景负载，核心 2 跑前台线程，核心 3 留给系统/其他任务
    if (ncores < 4) {
        std::cerr << "Need at least 4 cores for meaningful comparison.\n";
        return 1;
    }

    // 场景 1：前台不绑定，也没有后台负载
    // 作为基线，反映系统本身的最小抖动
    {
        auto s = measure_latency(foreground_core, false, {}, iterations);
        print_stats("Scenario 1: no affinity, no background load", s);
    }

    // 场景 2：前台不绑定，后台在核心 0、1 上烧 CPU
    // 操作系统可能把前台线程调度到核心 0/1，与后台线程抢资源
    {
        auto s = measure_latency(foreground_core, false, {0, 1}, iterations);
        print_stats("Scenario 2: no affinity, background on core 0,1", s);
    }

    // 场景 3：前台绑定到核心 2，后台在核心 0、1
    // 前台线程固定核心 2，不会被抢到核心 0/1，理想情况下抖动最小
    {
        auto s = measure_latency(foreground_core, true, {0, 1}, iterations);
        print_stats("Scenario 3: pinned to core 2, background on core 0,1", s);
    }

    // 场景 4：前台绑定到核心 2，但两个后台线程也绑定到核心 2
    // 说明：亲和性不是万能药；如果不隔离核心，同核心仍有竞争，效果反而更差
    {
        auto s = measure_latency(foreground_core, true, {2, 2}, iterations);
        print_stats("Scenario 4: pinned to core 2, background also on core 2", s);
    }

    return 0;
}
