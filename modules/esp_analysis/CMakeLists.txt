# ESP analysis module (removable analysis layer between the hypervisor core
# and the overlay). Standalone-configurable:
#   cmake -S modules/esp_analysis -B build/esp_analysis
cmake_minimum_required(VERSION 3.15)
project(hypersinv_esp_analysis LANGUAGES CXX)

option(HYPERSINV_BUILD_ESP_MODULE "Build the ESP analysis module" ON)

if(HYPERSINV_BUILD_ESP_MODULE)
    add_compile_definitions(MODULE_ESP)

    set(ESP_BASE_SOURCES
        esp_analysis.cpp
        entity_cache.cpp
        arc_raiders/decrypt.cpp
        arc_raiders/pointer_chains.cpp
        arc_raiders/camera.cpp
        arc_raiders/worldtoscreen.cpp
    )

    set(ESP_HARDCENED_SOURCES
        hardened/hardened_esp.cpp
    )

    option(HYPERSINV_BUILD_HARDCENED_ESP "Build hardened ESP" ON)

    # Hv* analysis APIs are not exported by the EFI payload yet, so the
    # module links self-contained (placeholder definitions live in
    # esp_analysis.cpp).
    add_library(esp_analysis STATIC ${ESP_BASE_SOURCES})
    target_include_directories(esp_analysis PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_compile_features(esp_analysis PRIVATE cxx_std_17)

    if(HYPERSINV_BUILD_HARDCENED_ESP)
        add_library(hardened_esp STATIC ${ESP_HARDCENED_SOURCES})
        target_include_directories(hardened_esp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(hardened_esp PRIVATE esp_analysis)
        target_compile_features(hardened_esp PRIVATE cxx_std_17)
    endif()

    set_target_properties(esp_analysis PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()