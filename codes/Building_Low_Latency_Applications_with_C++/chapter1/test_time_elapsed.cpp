#include <iostream>
#include <chrono>
#include <windows.h>

int main() {
    SetConsoleOutputCP(CP_UTF8);
    using namespace std::chrono;

    auto start = high_resolution_clock::now();

    // 要测量的代码
    volatile int sum = 0;
    for (int i = 0; i < 1'000'000; ++i) {
        sum += i;
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start).count();

    std::cout << "耗时: " << duration << " ms" << std::endl;
    std::cout << "单词平均：" << duration / 1'000'000.0 << " ms/单词" << std::endl;
}