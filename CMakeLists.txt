cmake_minimum_required(VERSION 3.20)

# Static MSVC runtime so the exe has no VC++ redistributable dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(MSoxyVPN VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "MSoxyVPN is a Windows-only application.")
endif()

add_executable(MSoxyVPN WIN32
  src/main.cpp
  src/settings.cpp
  src/tunnel.cpp
  resources/app.rc
)

target_compile_definitions(MSoxyVPN PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

target_link_libraries(MSoxyVPN PRIVATE ws2_32 iphlpapi comctl32)

if(MSVC)
  target_compile_options(MSoxyVPN PRIVATE /W4 /utf-8 /permissive-)
  # The manifest (admin elevation + visual styles) is embedded via app.rc
  target_link_options(MSoxyVPN PRIVATE /MANIFEST:NO)
elseif(MINGW)
  target_link_options(MSoxyVPN PRIVATE -municode -static)
endif()
