cmake_minimum_required(VERSION 3.20)
project(imgui_menu_editor CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_executable(imgui-menu-editor
  src/main.cpp src/editor.cpp src/model.cpp src/theme.cpp src/assets.cpp
  src/renderer.cpp src/exporter.cpp src/bridge.cpp src/tools.cpp
  runtime/mm_widgets.cpp)
target_include_directories(imgui-menu-editor PRIVATE src runtime third_party)
target_link_libraries(imgui-menu-editor PRIVATE imgui Threads::Threads)
target_compile_definitions(imgui-menu-editor PRIVATE MME_DATA_DIR="${CMAKE_BINARY_DIR}/data")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(imgui-menu-editor PRIVATE -Wall -Wno-invalid-offsetof)
endif()

# Runtime data next to the binary: fonts, widget runtime (for export), templates.
set(DATA_OUT ${CMAKE_BINARY_DIR}/data)
add_custom_target(mme_data ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DATA_OUT}/fonts ${DATA_OUT}/runtime ${DATA_OUT}/stb
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf ${IMGUI_DIR}/misc/fonts/Karla-Regular.ttf
          ${IMGUI_DIR}/misc/fonts/Cousine-Regular.ttf ${IMGUI_DIR}/misc/fonts/DroidSans.ttf
          ${CMAKE_SOURCE_DIR}/third_party/fa/fa-solid-900.ttf ${DATA_OUT}/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/runtime/mm_widgets.h ${CMAKE_SOURCE_DIR}/runtime/mm_widgets.cpp ${DATA_OUT}/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h ${DATA_OUT}/stb
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data ${DATA_OUT}
  VERBATIM)
