#include <iostream>
#include <array>

// 编译时确定大小的数组，无运行时开销
template <size_t N>
class FixedArray {
    public:
        int& operator[](size_t i) {
            return data_[i];
        }
        size_t size() const {
            return N;
        }
    private:
        std::array<int, N> data_;
};

int main() {
    FixedArray<10> arr;
    for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i);
    }

    // 编译后，上面的循环很可能被完全展开并优化
    // 等价于直接操作 10 个 int 变量

    return 0;
}