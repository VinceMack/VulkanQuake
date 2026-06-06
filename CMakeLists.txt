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

# SHADER COMPILATION
# Find the GLSL to SPIR-V compiler included in the Vulkan SDK
find_program(GLSLC glslc REQUIRED)

# Define our shader files
set(SHADER_FILES 
    shaders/mesh.vert 
    shaders/mesh.frag
)

set(SPIRV_FILES "")

# Tell CMake how to compile each shader into the build directory
foreach(SHADER ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(OUTPUT_SPV "${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}.spv")
    
    add_custom_command(
        OUTPUT ${OUTPUT_SPV}
        COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${OUTPUT_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling shader ${FILE_NAME} to SPIR-V..."
    )
    
    list(APPEND SPIRV_FILES ${OUTPUT_SPV})
endforeach()

# Create a custom target that forces shaders to compile before the engine builds
add_custom_target(CompileShaders DEPENDS ${SPIRV_FILES})

# Add your engine files
add_executable(VulkanQuake
    src/bspFormat.hpp
    src/main.cpp
    src/Map.cpp
    src/Map.hpp
    src/VirtualFileSystem.cpp
    src/VirtualFileSystem.hpp
    src/vk_mem_alloc.cpp
    src/GpuBuffer.hpp
    src/GpuBuffer.cpp
)

# Tell CMake to build the shaders BEFORE it builds VulkanQuake
add_dependencies(VulkanQuake CompileShaders)

# Link everything
target_link_libraries(VulkanQuake PRIVATE 
    Vulkan::Vulkan 
    vk-bootstrap::vk-bootstrap
    SDL3::SDL3
)