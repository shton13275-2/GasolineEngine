cmake_minimum_required(VERSION 3.24)

project(GasolineEngine
    VERSION 0.1.0
    DESCRIPTION "Gasoline Engine - a cross-platform 3D game engine"
    LANGUAGES CXX
)

option(ORIGINALENGINE_BUILD_APP "Build the OriginalEngine sample application" ON)
option(ORIGINALENGINE_USE_SDL3 "Use SDL3 for engine rendering, window, and input integration" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(originalengine
    src/engine/Application.cpp
    src/engine/Camera.cpp
    src/engine/CollisionWorld.cpp
    src/engine/GpuRenderer.cpp
    src/engine/Renderer3D.cpp
    src/engine/Scene.cpp
    src/engine/ObjMeshLoader.cpp
    src/engine/PpmImageLoader.cpp
)

target_include_directories(originalengine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(originalengine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(originalengine PRIVATE /W4 /permissive-)
else()
    target_compile_options(originalengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT ORIGINALENGINE_USE_SDL3)
    message(FATAL_ERROR
        "The engine library currently requires SDL3. ORIGINALENGINE_USE_SDL3=OFF is not supported.")
endif()

# SDL3 is a dependency of the engine library itself (Renderer3D and GpuRenderer),
# not only of the optional sample application.
include(FetchContent)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS app bundles must not depend on an unbundled SDL dylib.
    # Build SDL3 as a static library and link it into the executable.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(originalengine PUBLIC SDL3::SDL3-static)
else()
    target_link_libraries(originalengine PUBLIC SDL3::SDL3)
endif()

if(ORIGINALENGINE_BUILD_APP)
    add_executable(originalengine_app
        src/main.cpp
        src/engine/GpuDeviceProbe.cpp
    )
    target_link_libraries(originalengine_app PRIVATE originalengine)
    target_compile_definitions(originalengine_app PRIVATE ORIGINALENGINE_USE_SDL3=1)

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(originalengine_app PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.gasolineengine.demo"
            MACOSX_BUNDLE_BUNDLE_NAME "Gasoline Engine"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.gasolineengine.demo"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
        set_source_files_properties(
            assets/demo.obj
            assets/demo_texture.ppm
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets"
        )
        set_source_files_properties(
            shaders/Metal/Basic3D.vert.metal
            shaders/Metal/Basic3D.frag.metal
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/shaders"
        )
        target_sources(originalengine_app PRIVATE
            assets/demo.obj
            assets/demo_texture.ppm
            shaders/Metal/Basic3D.vert.metal
            shaders/Metal/Basic3D.frag.metal
        )
    else()
        add_custom_command(TARGET originalengine_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:originalengine_app>/assets
        )
    endif()

    if(WIN32)
        add_custom_command(TARGET originalengine_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:originalengine_app>
        )
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(originalengine_camera_tests tests/camera_tests.cpp)
    target_link_libraries(originalengine_camera_tests PRIVATE originalengine)
    add_test(NAME camera_controls COMMAND originalengine_camera_tests)

    add_executable(originalengine_collision_tests tests/collision_tests.cpp)
    target_link_libraries(originalengine_collision_tests PRIVATE originalengine)
    add_test(NAME collision_world COMMAND originalengine_collision_tests)

    add_executable(originalengine_scene_tests tests/scene_tests.cpp)
    target_link_libraries(originalengine_scene_tests PRIVATE originalengine)
    add_test(NAME scene_management COMMAND originalengine_scene_tests)

    add_executable(originalengine_scene_render_item_tests tests/scene_render_item_tests.cpp)
    target_link_libraries(originalengine_scene_render_item_tests PRIVATE originalengine)
    add_test(NAME scene_render_items COMMAND originalengine_scene_render_item_tests)

    add_executable(originalengine_obj_mesh_tests tests/obj_mesh_loader_tests.cpp)
    target_link_libraries(originalengine_obj_mesh_tests PRIVATE originalengine)
    add_test(NAME obj_mesh_loader COMMAND originalengine_obj_mesh_tests)

    add_executable(originalengine_ppm_image_tests tests/ppm_image_loader_tests.cpp)
    target_link_libraries(originalengine_ppm_image_tests PRIVATE originalengine)
    add_test(NAME ppm_image_loader COMMAND originalengine_ppm_image_tests)

    add_executable(originalengine_renderer3d_tests tests/renderer3d_tests.cpp)
    target_link_libraries(originalengine_renderer3d_tests PRIVATE originalengine)
    add_test(NAME renderer_software_size COMMAND originalengine_renderer3d_tests)

    add_executable(originalengine_gpu_geometry_tests tests/gpu_geometry_rules_tests.cpp)
    target_link_libraries(originalengine_gpu_geometry_tests PRIVATE originalengine)
    add_test(NAME gpu_geometry_rules COMMAND originalengine_gpu_geometry_tests)

    add_executable(originalengine_gpu_frame_recovery_tests tests/gpu_frame_recovery_tests.cpp)
    target_link_libraries(originalengine_gpu_frame_recovery_tests PRIVATE originalengine)
    add_test(NAME gpu_frame_recovery COMMAND originalengine_gpu_frame_recovery_tests)

    add_executable(originalengine_touch_overlay_tests tests/touch_overlay_tests.cpp)
    target_link_libraries(originalengine_touch_overlay_tests PRIVATE originalengine)
    add_test(NAME touch_overlay_hit_test COMMAND originalengine_touch_overlay_tests)

    add_executable(originalengine_backend_switch_input_tests tests/backend_switch_input_tests.cpp)
    target_link_libraries(originalengine_backend_switch_input_tests PRIVATE originalengine)
    add_test(NAME backend_switch_input COMMAND originalengine_backend_switch_input_tests)

    add_executable(originalengine_primitive_mesh_tests tests/primitive_mesh_tests.cpp)
    target_link_libraries(originalengine_primitive_mesh_tests PRIVATE originalengine)
    add_test(NAME primitive_meshes COMMAND originalengine_primitive_mesh_tests)

    add_executable(originalengine_gpu_render_diagnostics_tests tests/gpu_render_diagnostics_tests.cpp)
    target_link_libraries(originalengine_gpu_render_diagnostics_tests PRIVATE originalengine)
    add_test(NAME gpu_render_diagnostics COMMAND originalengine_gpu_render_diagnostics_tests)
endif()
