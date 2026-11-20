cmake_minimum_required(VERSION 3.24)
project(GdMacroMenu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

add_executable(GdMacroMenu WIN32
    src/main.cpp src/Config.cpp src/Process.cpp
    src/Input.cpp src/Macro.cpp src/Bot.cpp src/Sound.cpp)
target_compile_definitions(GdMacroMenu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(GdMacroMenu PRIVATE imgui winmm)
if(MSVC)
    target_compile_options(GdMacroMenu PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET GdMacroMenu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/gd_offsets.cfg $<TARGET_FILE_DIR:GdMacroMenu>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/thinkingspace2.gdm $<TARGET_FILE_DIR:GdMacroMenu>)
