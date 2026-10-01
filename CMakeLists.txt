cmake_minimum_required(VERSION 3.16)
project(truchet_loops CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

add_executable(truchet_loops main.cpp
    TruchetPathGenerator.hpp
    DodecagonTopology.hpp
    OctagonTopology.hpp
    HexagonTopology.hpp
    TopologyUtils.hpp
    RenderUtils.hpp)
target_link_libraries(truchet_loops PRIVATE 
    glm::glm 
    OpenGL::GL 
    glfw
)