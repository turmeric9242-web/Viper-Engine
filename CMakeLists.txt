cmake_minimum_required(VERSION 3.16)
project(ViperEngine C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no VC++ redistributable needed
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(VIPER_BUILD_TESTS "Build the headless logic tests" ON)

# GLFW + Dear ImGui are downloaded and built automatically (static, so viper.exe is a single file).
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.89.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# Engine modules. Each folder under src/ is one module; core logic has no GLFW/GL dependency.
set(VIPER_LOGIC_SRC
  src/scene/scene.c src/physics/physics.c src/blocks/blocks.c src/platform/input.c)
set(VIPER_PLATFORM_SRC
  src/platform/window.c src/platform/fs.c src/gfx/glload.c src/gfx/renderer.c
  src/gui/gui.cpp src/engine/engine.c src/main.c)

add_executable(viper ${VIPER_LOGIC_SRC} ${VIPER_PLATFORM_SRC})
target_include_directories(viper PRIVATE src)
target_link_libraries(viper PRIVATE glfw imgui)
if(WIN32)
  target_link_libraries(viper PRIVATE opengl32)
  if(MINGW)
    target_link_options(viper PRIVATE -static)
  endif()
elseif(NOT APPLE)
  find_package(OpenGL REQUIRED)
  target_link_libraries(viper PRIVATE OpenGL::GL m)
endif()

# Put scenes/ and scripts/ next to the executable.
add_custom_command(TARGET viper POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scenes $<TARGET_FILE_DIR:viper>/scenes
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:viper>/scripts)

if(VIPER_BUILD_TESTS)
  add_executable(viper_test tests/test_core.c ${VIPER_LOGIC_SRC})
  target_include_directories(viper_test PRIVATE src)
  if(NOT WIN32)
    target_link_libraries(viper_test PRIVATE m)
  endif()
endif()
