cmake_minimum_required(VERSION 3.20)
project(device_native_io LANGUAGES CXX)

add_library(device_native_io_core STATIC device_native_io_core.cpp)
target_compile_features(device_native_io_core PUBLIC cxx_std_17)
target_include_directories(device_native_io_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(device_native_io device_native_io.cpp)
target_link_libraries(device_native_io PRIVATE device_native_io_core setupapi advapi32)
target_compile_definitions(device_native_io PRIVATE UNICODE _UNICODE)

add_executable(device_native_io_core_tests tests/device_native_io_core_tests.cpp)
target_link_libraries(device_native_io_core_tests PRIVATE device_native_io_core)

if(MSVC)
    foreach(target device_native_io_core device_native_io device_native_io_core_tests)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()

enable_testing()
add_test(NAME device_native_io_core_tests COMMAND device_native_io_core_tests)
