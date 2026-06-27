cmake_minimum_required(VERSION 3.16)
project(flux_shader_sandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml ShaderTools)

qt_add_executable(shader_sandbox main.cpp)
target_link_libraries(shader_sandbox PRIVATE Qt6::Quick Qt6::Qml)

# Copy QML to build (exclude the shaders symlink — handled separately below)
file(COPY ${CMAKE_SOURCE_DIR}/main.qml DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test_fluid.qml DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/../../qml/ DESTINATION ${CMAKE_BINARY_DIR}
     PATTERN "shaders" EXCLUDE)
# Copy compiled shaders into build/shaders/ (from plugin build output)
file(COPY ${CMAKE_SOURCE_DIR}/../../plugin/build/FluxEngine/shaders/ DESTINATION ${CMAKE_BINARY_DIR}/shaders)
