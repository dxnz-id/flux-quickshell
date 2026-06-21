cmake_minimum_required(VERSION 3.20)
project(FluidSim VERSION 1.0.0 LANGUAGES CXX)

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
        COMMAND ${QSB_TOOL} --qt6 ${vert} -o ${qsb_output}
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

qt_add_qml_module(fluidsim
    URI FluidSim
    VERSION 1.0
    # NO_GENERATE_PLUGIN_SOURCE  -- let cmake auto-generate plugin
    NO_PLUGIN_OPTIONAL
    CLASS_NAME FluidSimPlugin
    SOURCES
        FluidSimItem.h
        FluidSimItem.cpp
        FluidSimEngine.h
        FluidSimEngine.cpp
        FluidSimShaders.h
        FluidSimShaders.cpp
)

set(QSB_DEST ${CMAKE_BINARY_DIR}/qml/FluidSim/shaders)
add_custom_command(
    OUTPUT ${QSB_DEST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QSB_DEST}
    COMMAND ${CMAKE_COMMAND} -E copy ${QSB_FILES} ${QSB_DEST}
    DEPENDS ${QSB_FILES}
    COMMENT "Copying QSB files to ${QSB_DEST}"
)

add_custom_target(fluidsim_qsb ALL DEPENDS ${QSB_FILES} ${QSB_DEST})
add_dependencies(fluidsimplugin fluidsim_qsb)

# Add Qt private header paths for QRhi access (Qt private headers)
# Pattern from Qt6GuiPrivate: adds both version-dir (for angle-bracket includes)
# and version-dir/module-dir (for quote includes within private headers)
set(QT6_VER "6.11.1")
foreach(tgt fluidsim fluidsimplugin)
    target_include_directories(${tgt} SYSTEM PRIVATE
        "/usr/include/qt6/QtGui/${QT6_VER}"
        "/usr/include/qt6/QtGui/${QT6_VER}/QtGui"
        "/usr/include/qt6/QtCore/${QT6_VER}"
        "/usr/include/qt6/QtCore/${QT6_VER}/QtCore"
    )
endforeach()

target_link_libraries(fluidsim PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui Qt6::ShaderTools)
target_link_libraries(fluidsimplugin PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui Qt6::ShaderTools)
