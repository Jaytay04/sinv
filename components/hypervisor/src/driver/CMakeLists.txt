# hypervisor_driver — KMDF kernel driver (.sys)
# Requires WDK. Links hypervisor_core static library.

find_package(WDK QUIET)
if(NOT WDK_FOUND)
    message(STATUS "WDK not found — skipping hypervisor_driver. Set WDK_CONTENT_ROOT for driver build.")
    return()
endif()

enable_language(ASM_MASM)

wdk_add_driver(hypervisor_driver
    SOURCES
        driver_main.c
        control_session_access.c
        control_session_namespace.c
        control_session_owner.c
        control_session_protocol.c
        control_session_protocol_kmdf.c
        control_session_request_frame.c
        control_session_response_frame.c
        vmx_platform_compatibility_adapter.c
        vmx_platform_readiness_adapter.c
        ioctl.c
        hook_table.c
        vmx_kernel_adapters.c
        vmx_persistent_basic_ops.c
        vmx_persistent_state_ops.c
        vmx_persistent_apply_ops.c
        vmx_persistent_composition.c
        vmx_persistent_runtime_ops.c
        vmx_persistent_runtime_lifecycle.c
        vmx_persistent_runtime_config.c
        vmx_cr3_target_provider.c
        vmx_process_target_owner.c
        vmx_cr3_boundary_roundtrip.c
        vmx_persistent_driver_owner.c
        vmx_persistent_serializer.c
        vmx_ept_native_adapter.c
        vmx_ept_invalidation_adapter.c
        vmx_ept_invalidation_execution.c
        vmx_vpid_native_adapter.c
        vmx_vpid_vmcs_adapter.c
        vmx_vpid_persistent_owner.c
        vmx_ept_hook_lifecycle.c
        vmx_ept_hook_roundtrip.c
        vmx_ept_installation_adapter.c
        vmx_ept_installation_execution.c
        vmx_ept_installation_lifecycle.c
        vmx_ept_runtime_adoption_adapter.c
        vmx_ept_installation_owner.c
        vmx_ept_storage_adapter.c
        vmx_ept_session_adapter.c
        vmx_ept_session_config_adapter.c
        vmx_ept_configured_session_adapter.c
        vmx_ept_composition_owner.c
        vmx_ept_persistent_composition_adapter.c
        vmx_ept_persistent_lifecycle_owner.c
        vmx_ept_persistent_device_lifecycle.c
        vmx_ept_persistent_device_hooks.c
        vmx_ept_persistent_device_teardown.c
        vmx_ept_persistent_device_start.c
        vmx_ept_persistent_device_status.c
        vmx_runtime.c
        vmx_launch.asm
        vmx_capture.asm
        vmx_ept_invept.asm
        vmx_vpid_invvpid.asm
        vmx_persistent_launch.asm
        vmx_persistent_exit.asm
    KMDF_VERSION_MAJOR 1
    KMDF_VERSION_MINOR 15
    LIBRARIES
)

target_include_directories(hypervisor_driver
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

# Kernel-mode target must NOT use standard C11 — WDK provides its own CRT.
# Strip /std:c11 and /RTC1 — WDK provides kernel-mode CRT; RTC is user-mode only
get_target_property(_driver_opts hypervisor_driver COMPILE_OPTIONS)
if(_driver_opts)
    list(FILTER _driver_opts EXCLUDE REGEX "/std:|/RTC")
    set(_driver_c_opts "")
    foreach(_driver_opt IN LISTS _driver_opts)
        list(APPEND _driver_c_opts "$<$<COMPILE_LANGUAGE:C,CXX>:${_driver_opt}>")
    endforeach()
    set_target_properties(hypervisor_driver PROPERTIES COMPILE_OPTIONS "${_driver_c_opts}")
endif()

target_link_libraries(hypervisor_driver
    PRIVATE hypervisor_core
)

target_link_options(hypervisor_driver PRIVATE /INTEGRITYCHECK)

target_compile_definitions(hypervisor_driver PRIVATE
    KMDF_VERSION_MAJOR=1
    KMDF_VERSION_MINOR=15
)

add_test(
    NAME test_vmx_launch_disassembly_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxLaunchDisassembly.ps1"
        -LinkerPath "${CMAKE_LINKER}"
        -ObjectPath "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/hypervisor_driver.dir/vmx_launch.asm.obj"
)

get_filename_component(_persistent_dumpbin_dir "${CMAKE_LINKER}" DIRECTORY)
set(_persistent_dumpbin_path "${_persistent_dumpbin_dir}/dumpbin.exe")
add_test(
    NAME test_vmx_persistent_disassembly_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentLaunchDisassembly.ps1"
        -LinkerPath "${CMAKE_LINKER}"
        -DumpbinPath "${_persistent_dumpbin_path}"
        -LaunchObjectPath "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/hypervisor_driver.dir/vmx_persistent_launch.asm.obj"
        -ExitObjectPath "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/hypervisor_driver.dir/vmx_persistent_exit.asm.obj"
)

add_test(
    NAME test_vmx_ept_invept_disassembly_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInveptDisassembly.ps1"
        -LinkerPath "${CMAKE_LINKER}"
        -ObjectPath "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/hypervisor_driver.dir/vmx_ept_invept.asm.obj"
)

add_test(
    NAME test_vmx_vpid_invvpid_disassembly_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidInvvpidDisassembly.ps1"
        -LinkerPath "${CMAKE_LINKER}"
        -ObjectPath "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/hypervisor_driver.dir/vmx_vpid_invvpid.asm.obj"
)
