cmake_minimum_required(VERSION 3.19)
project(skifree_sdl)

set(CMAKE_MODULE_PATH ${_MODULE_PATH} "${skifree_sdl_SOURCE_DIR}/cmake/")

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Add all c source files under the src directory

file(GLOB SOURCES "src/*.c")
file(GLOB resource_files "resources/*.bmp")

if (APPLE)

    # generate png file from ico
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ICONSKI.png
        COMMAND convert
        ARGS -flatten ${CMAKE_SOURCE_DIR}/resources/ICONSKI.ICO ${CMAKE_CURRENT_BINARY_DIR}/ICONSKI.PNG
        DEPENDS ${CMAKE_SOURCE_DIR}/resources/ICONSKI.ICO
    )

    # generate icns file from png
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ski32.icns
        COMMAND ${CMAKE_SOURCE_DIR}/tools/make-osx-icns.sh
        DEPENDS ICONSKI.png
    )

    set(skifree_sdl_ICON ${CMAKE_CURRENT_BINARY_DIR}/ski32.icns)
    set_source_files_properties(${skifree_sdl_ICON} PROPERTIES
       MACOSX_PACKAGE_LOCATION "Resources")

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${skifree_sdl_ICON})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUNDLE True
        MACOSX_BUNDLE_GUI_IDENTIFIER ${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_ICON_FILE ski32.icns
    )

    target_sources(${PROJECT_NAME} PRIVATE
        ski32.icns
    )

    INSTALL(CODE "
        include(BundleUtilities)
        fixup_bundle(${CMAKE_BINARY_DIR}/${PROJECT_NAME}.app \"\" \"/Library/Frameworks\")
        ")


else()
    add_executable(${PROJECT_NAME})
endif()

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.h"
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DRESOURCES_GLOB=resources/*" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedResources.cmake"
    DEPENDS ${resource_files}
)

target_sources(${PROJECT_NAME} PRIVATE
    src/skifree_decomp.c
    embedded_resources.h
)

# Add all headers files under the include directory
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SDL2IMAGE_INCLUDE_DIR}
    ${SDL2TTF_INCLUDE_DIR}
)

# Add compiler errors/warnings flags
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /ENTRY:mainCRTStartup)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -D_CRT_SECURE_NO_WARNINGS -DSDL_MAIN_HANDLED -DWIN32_LEAN_AND_MEAN)
# target_compile_options(${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:MSVC>:/W4 /WX>)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
endif()

target_link_libraries(${PROJECT_NAME} SDL2::SDL2 ${SDL2IMAGE_LIBRARY} ${SDL2TTF_LIBRARY})
