# ESP analysis module (removable analysis layer between the hypervisor core
# and the overlay).
#
# Two ways to build:
#   standalone  cmake -S modules/esp_analysis -B build/esp_analysis && cmake --build build/esp_analysis
#   root superbuild (default): the target esp_module is part of `combined_esp`
#                             and gets picked up by `all`.
cmake_minimum_required(VERSION 3.24)
project(hypersinv_esp_analysis LANGUAGES C CXX)

option(HYPERSINV_BUILD_ESP_MODULE "Build the ESP analysis module" ON)
option(HYPERSINV_BUILD_HARDCENED_ESP "Build hardened ESP" ON)

if(HYPERSINV_BUILD_ESP_MODULE)
    add_compile_definitions(MODULE_ESP)

    set(ESP_BASE_SOURCES
        esp_analysis.cpp
        entity_cache.cpp
        hv_bridge/hv_bridge.c
        arc_raiders/camera.cpp
        arc_raiders/decrypt.cpp
        arc_raiders/pointer_chains.cpp
        arc_raiders/worldtoscreen.cpp
    )

    set(ESP_HARDCENED_SOURCES
        hardened/hardened_esp.cpp
    )

    add_library(esp_analysis STATIC ${ESP_BASE_SOURCES})
    set_source_files_properties(hv_bridge/hv_bridge.c PROPERTIES LANGUAGE CXX)
    target_include_directories(esp_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # hv_bridge.c links the real HVR1/HVA1/mailbox ABI, so make those headers reachable.
    target_include_directories(esp_analysis PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..//components/hypervisor/src/driver/efi_runtime_companion
        ${CMAKE_CURRENT_SOURCE_DIR}/../..//components/hypervisor/src/efi
        ${CMAKE_CURRENT_SOURCE_DIR}/../..//tools/overlay/include
    )
    target_compile_features(esp_analysis PRIVATE cxx_std_17)
    target_link_libraries(esp_analysis PRIVATE bcrypt)
    set_target_properties(esp_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Convenience alias so callers can do `add_library(esp_module ALIAS esp_analysis)`.
    add_library(esp_module ALIAS esp_analysis)

    if(HYPERSINV_BUILD_HARDCENED_ESP)
        add_library(hardened_esp STATIC ${ESP_HARDCENED_SOURCES})
        target_include_directories(hardened_esp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(hardened_esp PRIVATE esp_analysis)
        target_compile_features(hardened_esp PRIVATE cxx_std_17)
    endif()
endif()
