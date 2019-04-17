cmake_minimum_required(VERSION 3.5.1)
project(vkraytrace)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include/")
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp" "${INCLUDE_DIR}/*.hpp")

add_executable(vkraytrace ${SOURCES})

add_subdirectory("${CMAKE_SOURCE_DIR}/src/shaders")

add_dependencies(vkraytrace shaders)

set_property(TARGET vkraytrace PROPERTY CXX_STANDARD 17)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.2)

target_include_directories(vkraytrace PRIVATE "${INCLUDE_DIR}")
target_link_libraries(vkraytrace Vulkan::Vulkan glfw assimp pthread)
