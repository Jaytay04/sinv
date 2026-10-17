cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(nic_vm_probe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "nic_vm_probe is a Windows-only native diagnostic.")
endif()

add_executable(nic_native_read native_read.cpp)
target_compile_features(nic_native_read PRIVATE cxx_std_17)
target_compile_definitions(nic_native_read PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
target_link_libraries(nic_native_read PRIVATE iphlpapi setupapi advapi32 ole32)
set_property(TARGET nic_native_read PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    target_compile_options(nic_native_read PRIVATE /W4 /WX /permissive- /utf-8)
endif()
