cmake_minimum_required(VERSION 3.14)
project(MatchingEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 优化级别
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -Wall -Wextra -DDEBUG")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -Wall -Wextra")
endif()

# 订单簿库（不含 MatchingEngine 撮合逻辑）
add_library(orderbook
    OrderBook.cpp
    OrderIndex.cpp
)

target_include_directories(orderbook PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 测试目标
add_executable(orderbook_test
    test/OrderBookTest.cpp
)
target_link_libraries(orderbook_test PRIVATE orderbook)
