# FindWDK.cmake
# Locates the Windows Driver Kit (WDK) for kernel-mode driver development.
# Sets:
#   WDK_FOUND          — TRUE if WDK is available
#   WDK_INCLUDE_DIRS   — include directories for kernel headers
#   WDK_LIBRARY_DIRS   — library directories for kernel libs
#   KMDF_VERSION_MAJOR — KMDF major version detected
#
# Usage:
#   find_package(WDK REQUIRED)
#
# Detects WDK via environment variables:
#   %WDKContentRoot%  — EWDK or standalone WDK
#   %WindowsSdkDir%   — Windows SDK (contains WDK headers + libs)
#   Fallback: scans "C:\Program Files (x86)\Windows Kits\10"

if(NOT WIN32)
    set(WDK_FOUND FALSE)
    if(WDK_FIND_REQUIRED)
        message(FATAL_ERROR "WDK is only available on Windows")
    endif()
    return()
endif()

#---------------------------------------------------------------------
# Cache entries
#---------------------------------------------------------------------
set(WDK_CONTENT_ROOT "" CACHE PATH "Path to WDK content root (EWDK or standalone WDK)")
set(WDK_VERSION     "" CACHE STRING "WDK version to use (e.g. 10.0.26100.0)")

#---------------------------------------------------------------------
# Attempt 1: WDKContentRoot environment variable (EWDK / standalone WDK)
#---------------------------------------------------------------------
if(NOT WDK_CONTENT_ROOT AND DEFINED ENV{WDKContentRoot})
    set(WDK_CONTENT_ROOT "$ENV{WDKContentRoot}" CACHE PATH "WDK content root from environment" FORCE)
endif()

if(WDK_CONTENT_ROOT AND EXISTS "${WDK_CONTENT_ROOT}")
    set(_wdk_inc "${WDK_CONTENT_ROOT}/Include")
    set(_wdk_lib "${WDK_CONTENT_ROOT}/Lib")

    if(EXISTS "${_wdk_inc}/km")
        set(_wdk_version_dir "")
        file(GLOB _wdk_ver_dirs LIST_DIRECTORIES true "${_wdk_inc}/10.*")
        if(_wdk_ver_dirs)
            list(GET _wdk_ver_dirs 0 _wdk_version_dir)
        endif()
        set(WDK_INCLUDE_DIRS "${_wdk_inc}/km" "${_wdk_inc}/km/crt" "${_wdk_inc}/shared" "${_wdk_version_dir}/km" "${_wdk_version_dir}/shared")
        set(WDK_LIBRARY_DIRS "${_wdk_lib}/km/x64")
        set(WDK_FOUND TRUE)
        if(NOT WDK_KITS_BASE)
            set(WDK_KITS_BASE "${WDK_CONTENT_ROOT}" CACHE INTERNAL "Windows Kits base path" FORCE)
        endif()
    endif()
endif()

#---------------------------------------------------------------------
# Attempt 2: WindowsSdkDir environment variable
#---------------------------------------------------------------------
if(NOT WDK_FOUND AND DEFINED ENV{WindowsSdkDir})
    set(_wkit "$ENV{WindowsSdkDir}")
    if(NOT WDK_VERSION)
        # Try to detect latest installed WDK version
        file(GLOB _wdk_versions LIST_DIRECTORIES true "${_wkit}Include/10.*")
        if(_wdk_versions)
            list(LENGTH _wdk_versions _num_versions)
            list(SORT _wdk_versions)
            list(GET _wdk_versions -1 _latest)
            get_filename_component(WDK_VERSION "${_latest}" NAME)
            set(WDK_VERSION "${WDK_VERSION}" CACHE STRING "WDK version" FORCE)
        endif()
    endif()

    if(WDK_VERSION AND EXISTS "${_wkit}Include/${WDK_VERSION}/km")
        set(WDK_INCLUDE_DIRS
            "${_wkit}Include/${WDK_VERSION}/km"
            "${_wkit}Include/${WDK_VERSION}/km/crt"
            "${_wkit}Include/${WDK_VERSION}/shared"
            "${_wkit}Include/${WDK_VERSION}/um"
            "${_wkit}Include/wdf/kmdf/1.15"
        )
        set(WDK_LIBRARY_DIRS "${_wkit}Lib/${WDK_VERSION}/km/x64")
        set(WDK_FOUND TRUE)
        set(KMDF_VERSION_MAJOR 1)
        if(NOT WDK_KITS_BASE)
            set(WDK_KITS_BASE "${_wkit}" CACHE INTERNAL "Windows Kits base path" FORCE)
        endif()
    endif()
endif()

#---------------------------------------------------------------------
# Attempt 3: Hardcoded fallback scan
#---------------------------------------------------------------------
if(NOT WDK_FOUND)
    set(_kits_base "C:/Program Files (x86)/Windows Kits/10")
    if(NOT WDK_VERSION)
        file(GLOB _wdk_versions LIST_DIRECTORIES true "${_kits_base}/Include/10.*")
        if(_wdk_versions)
            list(LENGTH _wdk_versions _num_versions)
            list(SORT _wdk_versions)
            list(GET _wdk_versions -1 _latest)
            get_filename_component(WDK_VERSION "${_latest}" NAME)
            set(WDK_VERSION "${WDK_VERSION}" CACHE STRING "WDK version" FORCE)
        endif()
    endif()

    if(WDK_VERSION AND EXISTS "${_kits_base}/Include/${WDK_VERSION}/km")
        set(WDK_INCLUDE_DIRS
            "${_kits_base}/Include/${WDK_VERSION}/km"
            "${_kits_base}/Include/${WDK_VERSION}/km/crt"
            "${_kits_base}/Include/${WDK_VERSION}/shared"
            "${_kits_base}/Include/${WDK_VERSION}/um"
            "${_kits_base}/Include/wdf/kmdf/1.15"
        )
        set(WDK_LIBRARY_DIRS "${_kits_base}/Lib/${WDK_VERSION}/km/x64")
        set(WDK_FOUND TRUE)
        set(KMDF_VERSION_MAJOR 1)
        # Save the base path for wdk_add_driver use
        if(NOT WDK_KITS_BASE)
            set(WDK_KITS_BASE "${_kits_base}" CACHE INTERNAL "Windows Kits base path" FORCE)
        endif()
    endif()
endif()

#---------------------------------------------------------------------
# Report
#---------------------------------------------------------------------
if(WDK_FOUND)
    message(STATUS "Found WDK version ${WDK_VERSION}")
    message(STATUS "  Include: ${WDK_INCLUDE_DIRS}")
    message(STATUS "  Library: ${WDK_LIBRARY_DIRS}")
elseif(WDK_FIND_REQUIRED)
    message(FATAL_ERROR "WDK not found. Set WDK_CONTENT_ROOT or ensure Windows SDK is installed with WDK support.")
else()
    message(STATUS "WDK not found — driver target will be skipped")
endif()

#---------------------------------------------------------------------
# Define wdk_add_driver() helper
#---------------------------------------------------------------------
function(wdk_add_driver TARGET)
    set(options "")
    set(oneValueArgs KMDF_VERSION_MAJOR KMDF_VERSION_MINOR)
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(WDK_DRV "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${TARGET} SHARED ${WDK_DRV_SOURCES})

    target_compile_definitions(${TARGET} PRIVATE
        UNICODE
        _UNICODE
        _AMD64_
        _WIN64
    )

    target_include_directories(${TARGET} PRIVATE ${WDK_INCLUDE_DIRS})
    target_link_directories(${TARGET} PRIVATE ${WDK_LIBRARY_DIRS})

    target_link_libraries(${TARGET} PRIVATE
        ntoskrnl.lib
        hal.lib
        wmilib.lib
        "${WDK_LIBRARY_DIRS}/BufferOverflowK.lib"
        ${WDK_DRV_LIBRARIES}
    )

    if(WDK_DRV_KMDF_VERSION_MAJOR)
        target_compile_definitions(${TARGET} PRIVATE
            KMDF_VERSION_MAJOR=${WDK_DRV_KMDF_VERSION_MAJOR}
        )
        # Link KMDF libraries when KMDF version is specified
        set(_kmdf_ver "${WDK_DRV_KMDF_VERSION_MAJOR}")
        if(WDK_DRV_KMDF_VERSION_MINOR)
            set(_kmdf_ver "${_kmdf_ver}.${WDK_DRV_KMDF_VERSION_MINOR}")
        endif()
        target_link_libraries(${TARGET} PRIVATE
            "${WDK_KITS_BASE}/Lib/wdf/kmdf/x64/${_kmdf_ver}/wdfdriverentry.lib"
            "${WDK_KITS_BASE}/Lib/wdf/kmdf/x64/${_kmdf_ver}/wdfldr.lib"
        )
    endif()
    if(WDK_DRV_KMDF_VERSION_MINOR)
        target_compile_definitions(${TARGET} PRIVATE
            KMDF_VERSION_MINOR=${WDK_DRV_KMDF_VERSION_MINOR}
        )
    endif()

    # Kernel-mode compile flags
    target_compile_options(${TARGET} PRIVATE
        /kernel       # Kernel mode code generation
        /GS-          # Buffer security checks disabled (kernel needs BufferOverflowK.lib)
        /Gz           # __stdcall calling convention
        /Zp8          # 8-byte struct alignment
        /W4           # Warning level 4
        /WX           # Treat warnings as errors
        /wd4100       # Suppress: unreferenced formal parameter
        /wd4324       # Suppress: KMDF header alignment padding
        /wd4083       # Suppress: MSVC pragma in WDK CRT headers
        /wd4005       # Suppress: macro redefinition (WDK vs MSVC CRT)
    )

    set_target_properties(${TARGET} PROPERTIES
        SUFFIX ".sys"
        PREFIX ""
        IMPORT_PREFIX ""
        LINK_FLAGS "/DRIVER /SUBSYSTEM:NATIVE /NODEFAULTLIB /MANIFEST:NO /INCREMENTAL:NO /ENTRY:FxDriverEntry"
        LINKER_LANGUAGE "C"
    )
endfunction()
