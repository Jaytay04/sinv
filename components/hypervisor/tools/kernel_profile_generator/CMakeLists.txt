add_executable(hypersinv_kernel_profile_generator
    kernel_profile_generator.cpp)
target_compile_features(hypersinv_kernel_profile_generator PRIVATE cxx_std_17)
target_compile_definitions(hypersinv_kernel_profile_generator PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN)
target_compile_options(hypersinv_kernel_profile_generator PRIVATE
    /W4 /WX /EHsc /permissive-)
target_link_libraries(hypersinv_kernel_profile_generator PRIVATE Dbghelp.lib)

set(_hypersinv_dia_root "${CMAKE_GENERATOR_INSTANCE}/DIA SDK")
set(_hypersinv_dia_dll "${_hypersinv_dia_root}/bin/amd64/msdia140.dll")
find_path(HYPERSINV_DIA_INCLUDE_DIR dia2.h
    PATHS "${_hypersinv_dia_root}/include"
    NO_DEFAULT_PATH)
find_library(HYPERSINV_DIA_GUIDS_LIBRARY diaguids
    PATHS "${_hypersinv_dia_root}/lib/amd64"
    NO_DEFAULT_PATH)
if(HYPERSINV_DIA_INCLUDE_DIR AND HYPERSINV_DIA_GUIDS_LIBRARY AND
        EXISTS "${_hypersinv_dia_dll}")
    target_compile_definitions(hypersinv_kernel_profile_generator PRIVATE
        HYPERSINV_HAVE_DIA=1
        HYPERSINV_DIA_DLL_PATH=L\"${_hypersinv_dia_dll}\")
    target_include_directories(hypersinv_kernel_profile_generator PRIVATE
        "${HYPERSINV_DIA_INCLUDE_DIR}")
    target_link_libraries(hypersinv_kernel_profile_generator PRIVATE
        Ole32.lib
        "${HYPERSINV_DIA_GUIDS_LIBRARY}")
else()
    target_compile_definitions(hypersinv_kernel_profile_generator PRIVATE
        HYPERSINV_HAVE_DIA=0)
endif()
