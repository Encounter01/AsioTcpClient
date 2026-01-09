cmake_minimum_required(VERSION 3.14)
project(AsioTcpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Boost header-only 模式（不需要编译库）
set(BOOST_ROOT "D:/softcpp/boost_1_81_0")
set(Boost_INCLUDE_DIR "D:/softcpp/boost_1_81_0")

# 查找 Boost（仅头文件）
find_package(Boost 1.81.0 REQUIRED)

# 查找线程库
find_package(Threads REQUIRED)

# 包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${Boost_INCLUDE_DIR})

# 源文件
set(LIB_SOURCES
    src/TcpClient.cpp
)

# 创建静态库
add_library(asioclient STATIC ${LIB_SOURCES})
target_include_directories(asioclient PUBLIC ${PROJECT_SOURCE_DIR}/include ${Boost_INCLUDE_DIR})

# 定义 header-only 模式宏
target_compile_definitions(asioclient PUBLIC BOOST_SYSTEM_NO_LIB BOOST_ERROR_CODE_HEADER_ONLY)

# Windows 平台需要链接 ws2_32
if(WIN32)
    target_compile_definitions(asioclient PUBLIC _WIN32_WINNT=0x0601)
    target_link_libraries(asioclient PUBLIC ws2_32 wsock32)
endif()

target_link_libraries(asioclient PUBLIC Threads::Threads)

# 示例程序
add_executable(tcp_client_example examples/main.cpp)
target_link_libraries(tcp_client_example PRIVATE asioclient)

# 设置输出目录
set_target_properties(asioclient PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib
)

set_target_properties(tcp_client_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)
