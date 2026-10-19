cmake_minimum_required(VERSION 3.24)

project(hypersinv_efi_inventory VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "hypersinv_efi_inventory is a Windows-only tool")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(EFI_INVENTORY_BUILD_COLLECTOR "Build the optional preboot EFI collector" ON)

add_executable(efi_inventory
    src/main.cpp
    src/inventory.cpp
    src/backup.cpp
    src/secure_pipe.cpp
    src/runtime_variables.cpp
    ../hwid_spoofer/src/device_identifiers.cpp
    ../hwid_spoofer/read_only_inventory.cpp)
target_include_directories(efi_inventory PRIVATE include ../hwid_spoofer ../hwid_spoofer/include)
target_compile_definitions(efi_inventory PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(efi_inventory PRIVATE
    advapi32 bcrypt crypt32 ole32 oleaut32 wbemuuid shell32)
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
    add_executable(efi_runtime_variables_tests
        tests/runtime_variables_tests.cpp src/runtime_variables.cpp src/inventory.cpp)
    target_include_directories(efi_runtime_variables_tests PRIVATE include)
    target_compile_definitions(efi_runtime_variables_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(efi_runtime_variables_tests PRIVATE advapi32 ole32)
    add_test(NAME efi_runtime_variables_contract COMMAND efi_runtime_variables_tests)

    add_executable(windows_device_identifiers_tests
        ../hwid_spoofer/tests/device_identifiers_tests.cpp
        ../hwid_spoofer/src/device_identifiers.cpp)
    target_include_directories(windows_device_identifiers_tests PRIVATE
        ../hwid_spoofer/include)
    target_compile_definitions(windows_device_identifiers_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(windows_device_identifiers_tests PRIVATE
        wbemuuid ole32 oleaut32)
    add_test(NAME windows_device_identifiers_contract COMMAND windows_device_identifiers_tests)

    add_executable(runtime_identifier_mapping_tests
        ../hwid_spoofer/tests/read_only_inventory_tests.cpp
        ../hwid_spoofer/read_only_inventory.cpp)
    target_include_directories(runtime_identifier_mapping_tests PRIVATE
        include ../hwid_spoofer ../hwid_spoofer/include)
    target_compile_definitions(runtime_identifier_mapping_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(runtime_identifier_mapping_tests PRIVATE ole32)
    add_test(NAME runtime_identifier_mapping_contract COMMAND runtime_identifier_mapping_tests)
    add_executable(hwid_config_contract_tests
        ../hwid_spoofer/tests/hwid_config_contract_tests.cpp
        ../hwid_spoofer/hwid_spoofer.c)
    target_include_directories(hwid_config_contract_tests PRIVATE ../hwid_spoofer)
    target_compile_definitions(hwid_config_contract_tests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(hwid_config_contract_tests PRIVATE /W4 /WX /utf-8)
    endif()
    add_test(NAME hwid_config_contract COMMAND hwid_config_contract_tests)

    if(MSVC)
        foreach(contract_target IN ITEMS efi_runtime_variables_tests
                windows_device_identifiers_tests runtime_identifier_mapping_tests)
            target_compile_options(${contract_target} PRIVATE /W4 /WX /permissive- /utf-8)
        endforeach()
    endif()

endif()

if(EFI_INVENTORY_BUILD_COLLECTOR)
    add_subdirectory(efi)
endif()
