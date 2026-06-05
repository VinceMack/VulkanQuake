# CMakeLists.txt - CMake configuration for VulkanQuake project
cmake_minimum_required(VERSION 4.3)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(VulkanQuake)

# 1. Create the target first
add_executable(VulkanQuake)

# 2. Append additional files to it later
target_sources(VulkanQuake PRIVATE 
    src/main.cpp
)