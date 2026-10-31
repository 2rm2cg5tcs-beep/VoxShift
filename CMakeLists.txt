cmake_minimum_required(VERSION 3.21)
project(Pressvppp VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- portable DSP core
add_library(voxdsp STATIC
  src/dsp/Denoiser.cpp
  src/dsp/PsolaVoice.cpp
  src/dsp/SpectralVoice.cpp
  src/dsp/VoiceChain.cpp
  src/dsp/Presets.cpp)
target_include_directories(voxdsp PUBLIC src third_party)

if(MSVC)
  # UTF-8 sources, static CRT (no VC++ redistributable needed), fast float math
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_property(TARGET voxdsp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(voxdsp PUBLIC /utf-8 /W3 /permissive- /Zc:__cplusplus /O2 /fp:precise /EHsc
                                       /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DUNICODE /D_UNICODE)
else()
  target_compile_options(voxdsp PRIVATE -O2 -Wall -Wextra)
endif()

add_executable(dsp_tests tests/dsp_tests.cpp)
target_link_libraries(dsp_tests PRIVATE voxdsp)
if(MSVC)
  set_property(TARGET dsp_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------- Windows app
if(WIN32)
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_win32.cpp
    third_party/imgui/imgui_impl_dx11.cpp)
  target_include_directories(imgui PUBLIC third_party third_party/imgui)
  if(MSVC)
    set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(imgui PRIVATE /utf-8 /O2 /DUNICODE /D_UNICODE)
  endif()

  add_executable(Pressvppp WIN32
    src/app/main.cpp
    src/app/App.cpp
    src/app/Ui.cpp
    src/app/Widgets.cpp
    src/app/AudioEngine.cpp
    src/app/Devices.cpp
    src/app/Config.cpp
    src/app/Platform.cpp
    src/app/Tray.cpp
    src/app/VbCable.cpp
    src/app/IconGen.cpp
    res/app.rc)
  target_link_libraries(Pressvppp PRIVATE voxdsp imgui
    d3d11 dxgi d3dcompiler dwmapi ole32 uuid avrt winmm shell32 urlmon wininet user32 gdi32 advapi32 propsys)
  if(MSVC)
    set_property(TARGET Pressvppp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # the manifest is embedded via app.rc
    target_link_options(Pressvppp PRIVATE /MANIFEST:NO)
  else()
    target_compile_options(Pressvppp PRIVATE -municode -DUNICODE -D_UNICODE -DNOMINMAX)
    target_link_options(Pressvppp PRIVATE -municode -static -static-libgcc -static-libstdc++)
  endif()
endif()
