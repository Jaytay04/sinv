cmake_minimum_required(VERSION 3.24)

project(hypersinv_overlay
    VERSION 1.0.0
    DESCRIPTION "Transparent host-side visualization overlay"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "hypersinv_overlay is a Windows-only application")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hypersinv_overlay WIN32
    src/main.cpp
    src/data_transform.cpp
    src/d3d_renderer.cpp
    src/overlay_window.cpp
    src/shared_memory_reader.cpp
)

target_include_directories(hypersinv_overlay PRIVATE include)
target_compile_definitions(hypersinv_overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(hypersinv_overlay PRIVATE
    bcrypt
    d2d1
    d3d11
    dwrite
    dwmapi
    dxgi
    shell32
    user32
)

if(MSVC)
    target_compile_options(hypersinv_overlay PRIVATE /W4 /WX /permissive- /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(hypersinv_overlay_protocol_tests
        tests/protocol_tests.cpp
        src/data_transform.cpp
        src/shared_memory_publisher.cpp
        src/shared_memory_reader.cpp
    )
    target_link_libraries(hypersinv_overlay_protocol_tests PRIVATE advapi32)
    target_include_directories(hypersinv_overlay_protocol_tests PRIVATE include)
    target_compile_definitions(hypersinv_overlay_protocol_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(hypersinv_overlay_protocol_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME hypersinv_overlay_protocol_tests
        COMMAND hypersinv_overlay_protocol_tests)

    add_executable(hypersinv_overlay_transform_tests
        tests/transform_tests.cpp
        src/data_transform.cpp
    )
    target_include_directories(hypersinv_overlay_transform_tests PRIVATE include)
    target_compile_definitions(hypersinv_overlay_transform_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(hypersinv_overlay_transform_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME hypersinv_overlay_transform_tests
        COMMAND hypersinv_overlay_transform_tests)

    add_executable(hypersinv_overlay_publisher_tests
        tests/publisher_tests.cpp
        src/shared_memory_publisher.cpp
        src/shared_memory_reader.cpp
    )
    target_link_libraries(hypersinv_overlay_publisher_tests PRIVATE advapi32)
    target_include_directories(hypersinv_overlay_publisher_tests PRIVATE include)
    target_compile_definitions(hypersinv_overlay_publisher_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(hypersinv_overlay_publisher_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME hypersinv_overlay_publisher_tests
        COMMAND hypersinv_overlay_publisher_tests)

    add_executable(hypersinv_overlay_window_tests
        tests/window_renderer_tests.cpp
        src/data_transform.cpp
        src/d3d_renderer.cpp
        src/overlay_window.cpp
        src/shared_memory_publisher.cpp
        src/shared_memory_reader.cpp
    )
    target_include_directories(hypersinv_overlay_window_tests PRIVATE include)
    target_compile_definitions(hypersinv_overlay_window_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(hypersinv_overlay_window_tests PRIVATE
        advapi32
        bcrypt
        d2d1
        d3d11
        dwrite
        dwmapi
        dxgi
        user32
    )
    if(MSVC)
        target_compile_options(hypersinv_overlay_window_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME hypersinv_overlay_window_tests
        COMMAND hypersinv_overlay_window_tests)

    find_program(OVERLAY_POWERSHELL_EXECUTABLE NAMES powershell.exe)
    if(OVERLAY_POWERSHELL_EXECUTABLE)
        add_test(NAME hypersinv_overlay_source_contracts
            COMMAND "${OVERLAY_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-OverlaySourceContracts.ps1"
                -SourceRoot "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
