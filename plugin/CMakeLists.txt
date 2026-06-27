cmake_minimum_required(VERSION 3.20)
project(FluxEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Gui ShaderTools)

find_program(QSB_TOOL qsb PATHS /usr/lib/qt6/bin /usr/bin)

file(GLOB VERTEX_SHADERS shaders/*.vert)
foreach(vert ${VERTEX_SHADERS})
    get_filename_component(name ${vert} NAME_WE)
    set(qsb_output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.qsb)
    list(APPEND QSB_FILES ${qsb_output})
    add_custom_command(
        OUTPUT ${qsb_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${QSB_TOOL} --glsl "440" ${vert} -o ${qsb_output}
        MAIN_DEPENDENCY ${vert}
        COMMENT "Baking ${name}.vert -> ${name}.qsb"
    )
endforeach()

file(GLOB FRAGMENT_SHADERS shaders/*.frag)
foreach(frag ${FRAGMENT_SHADERS})
    get_filename_component(name ${frag} NAME_WE)
    set(qsb_output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.qsb)
    list(APPEND QSB_FILES ${qsb_output})
    add_custom_command(
        OUTPUT ${qsb_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${QSB_TOOL} --glsl "440" ${frag} -o ${qsb_output}
        MAIN_DEPENDENCY ${frag}
        COMMENT "Baking ${name}.frag -> ${name}.qsb"
    )
endforeach()

file(GLOB COMPUTE_SHADERS shaders/*.comp)
foreach(comp ${COMPUTE_SHADERS})
    get_filename_component(name ${comp} NAME_WE)
    set(qsb_output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.qsb)
    list(APPEND QSB_FILES ${qsb_output})
    add_custom_command(
        OUTPUT ${qsb_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${QSB_TOOL} --glsl "440" ${comp} -o ${qsb_output}
        MAIN_DEPENDENCY ${comp}
        COMMENT "Baking ${name}.comp -> ${name}.qsb"
    )
endforeach()

qt_add_qml_module(fluxengine
    URI FluxEngine
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/FluxEngine"
    # NO_GENERATE_PLUGIN_SOURCE  -- let cmake auto-generate plugin
    NO_PLUGIN_OPTIONAL
    CLASS_NAME FluxEnginePlugin
    SOURCES
        FluxItem.h
        FluxItem.cpp
        FluxEngine.h
        FluxEngine.cpp
        FluxShaders.h
        FluxShaders.cpp
)

set(QSB_DEST ${CMAKE_BINARY_DIR}/FluxEngine/shaders)
add_custom_command(
    OUTPUT ${QSB_DEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QSB_DEST}
    COMMAND ${CMAKE_COMMAND} -E copy ${QSB_FILES} ${QSB_DEST}
    DEPENDS ${QSB_FILES}
    COMMENT "Copying QSB files to ${QSB_DEST}"
)

add_custom_target(fluxengine_qsb ALL DEPENDS ${QSB_FILES} ${QSB_DEST})
add_dependencies(fluxengineplugin fluxengine_qsb)

# Qt private headers (QRhi access). Try CMake targets first (works with aqtinstall),
# fall back to hardcoded paths for system Qt installations (Arch, etc.)
find_package(Qt6 QUIET COMPONENTS GuiPrivate CorePrivate)
if(TARGET Qt6::GuiPrivate)
    foreach(tgt fluxengine fluxengineplugin)
        target_link_libraries(${tgt} PRIVATE Qt6::GuiPrivate Qt6::CorePrivate)
    endforeach()
else()
    set(QT6_VER "6.11.1")
    foreach(tgt fluxengine fluxengineplugin)
        target_include_directories(${tgt} SYSTEM PRIVATE
            "/usr/include/qt6/QtGui/${QT6_VER}"
            "/usr/include/qt6/QtGui/${QT6_VER}/QtGui"
            "/usr/include/qt6/QtCore/${QT6_VER}"
            "/usr/include/qt6/QtCore/${QT6_VER}/QtCore"
        )
    endforeach()
endif()

target_link_libraries(fluxengine PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui Qt6::ShaderTools)
target_link_libraries(fluxengineplugin PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui Qt6::ShaderTools)
