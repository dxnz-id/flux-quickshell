cmake_minimum_required(VERSION 3.16)
project(flux_shader_sandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml ShaderTools)

qt_add_executable(shader_sandbox main.cpp)
target_link_libraries(shader_sandbox PRIVATE Qt6::Quick Qt6::Qml)

# Copy QML + shaders to build directory at configure time
file(COPY ${CMAKE_SOURCE_DIR}/main.qml DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/../../shaders/compiled/ DESTINATION ${CMAKE_BINARY_DIR}/shaders)
