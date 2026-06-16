cmake_minimum_required(VERSION 3.20)
project(Equirectangular_to_Cubemap)

# Require C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find OpenCV library
find_package(OpenCV REQUIRED)

# Create the executable
add_executable(Equirectangular_to_Cubemap main.cpp)

# Link OpenCV libraries
target_link_libraries(Equirectangular_to_Cubemap PRIVATE ${OpenCV_LIBS})

# Options to toggle #ifdef blocks at compile time
option(USE_GUI "Enable OpenCV GUI preview" ON)
option(ENABLE_PROFILING "Enable execution time profiling" ON)

if(USE_GUI)
    target_compile_definitions(Equirectangular_to_Cubemap PRIVATE USE_GUI)
endif()

if(ENABLE_PROFILING)
    target_compile_definitions(Equirectangular_to_Cubemap PRIVATE ENABLE_PROFILING)
endif()