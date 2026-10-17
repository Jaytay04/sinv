cmake_minimum_required(VERSION 3.24)

project(hypersinv_efi_inventory VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "hypersinv_efi_inventory is a Windows-only tool")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(efi_inventory
    src/main.cpp
    src/inventory.cpp
    src/backup.cpp
    src/secure_pipe.cpp)
target_include_directories(efi_inventory PRIVATE include)
target_compile_definitions(efi_inventory PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(efi_inventory PRIVATE
    advapi32 bcrypt crypt32 ole32 shell32)
if(MSVC)
    target_compile_options(efi_inventory PRIVATE /W4 /WX /permissive- /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(efi_inventory_tests
        tests/inventory_tests.cpp
        src/inventory.cpp)
    target_include_directories(efi_inventory_tests PRIVATE include)
    target_compile_definitions(efi_inventory_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(efi_inventory_tests PRIVATE ole32)
    if(MSVC)
        target_compile_options(efi_inventory_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME efi_inventory_parser COMMAND efi_inventory_tests)

    add_executable(efi_inventory_pipe_tests
        tests/secure_pipe_tests.cpp
        src/secure_pipe.cpp)
    target_include_directories(efi_inventory_pipe_tests PRIVATE include)
    target_compile_definitions(efi_inventory_pipe_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        HYPERSINV_EFI_INVENTORY_TESTING)
    target_link_libraries(efi_inventory_pipe_tests PRIVATE
        advapi32 bcrypt crypt32 ole32 shell32)
    if(MSVC)
        target_compile_options(efi_inventory_pipe_tests PRIVATE
            /W4 /WX /permissive- /utf-8)
    endif()
    add_test(NAME efi_inventory_pipe_contract COMMAND efi_inventory_pipe_tests)
endif()

add_subdirectory(efi)
