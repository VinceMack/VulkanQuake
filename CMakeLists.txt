# Set the minimum CMake version and project details
cmake_minimum_required(VERSION 4.3)
project(VulkanQuake CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan (From LunarG SDK installation)
find_package(Vulkan REQUIRED)

# Include FetchContent
include(FetchContent)

# Fetch vk-bootstrap
FetchContent_Declare(
    fetch_vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG        v1.4.352 # Make sure this tag matches the repo exactly
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fetch_vk_bootstrap)

# Turn off SDL's extra fluff BEFORE we fetch it to save compile time
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples" FORCE)
set(SDL_SHARED OFF CACHE BOOL "Disable SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL static library" FORCE)

# Fetch SDL3
FetchContent_Declare(
    fetch_sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG        release-3.4.10 # Make sure this tag matches the repo exactly
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fetch_sdl3)

# Add your engine files
add_executable(VulkanQuake 
    src/main.cpp
    src/vk_mem_alloc.cpp
    src/VirtualFileSystem.cpp
    src/VirtualFileSystem.hpp
)

# Link everything
target_link_libraries(VulkanQuake PRIVATE 
    Vulkan::Vulkan 
    vk-bootstrap::vk-bootstrap
    SDL3::SDL3
)