# Builds the exported menu in a small GLFW + OpenGL3 window.
#   cmake -B build -DIMGUI_DIR=/path/to/imgui && cmake --build build && ./build/menu_preview
cmake_minimum_required(VERSION 3.16)
project(menu_preview CXX)
set(CMAKE_CXX_STANDARD 17)

set(IMGUI_DIR "" CACHE PATH "Path to a Dear ImGui (1.92+) checkout")
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
  message(FATAL_ERROR "Set -DIMGUI_DIR=/path/to/imgui (Dear ImGui 1.92 or newer)")
endif()

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(menu_preview
  main.cpp
  ../mm_widgets.cpp
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(menu_preview PRIVATE .. ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(menu_preview PRIVATE ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets")
target_link_libraries(menu_preview PRIVATE glfw OpenGL::GL)
