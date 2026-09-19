# hypervisor_core — Pure C11 static library (NO WDK dependency)
# Unit-testable in user mode. Contains the production policy/model layer.
# The historical VM-exit simulation is compiled only by test_vmexit.

# Source files — add incremental as implemented
# PR 1: Foundation
set(HV_CORE_SOURCES
    vmx_init.c
    cpu_features.c
)

# PR 2: EPT foundation
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    ept_manager.c
)
# PR 3: Transparency
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    cpuid_handler.c
    msr_handler.c
    vmx_msr_bitmap_policy.c
    cr3_handler.c
    cet_suppress.c
)

# Pure CR3 target-selection model consumed through the authenticated route owner.
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    control_capabilities_policy.c
    vmx_platform_compatibility.c
    vmx_process_target_policy.c
    vmx_cr3_target_selection.c
    vmx_cr3_exit_decode.c
    vmx_cr3_residency.c
    vmx_cr3_route_plan.c
    vmx_cr3_control_policy.c
    vmx_cr3_public_status.c
    vmx_cr3_route_owner.c
    vmx_cr3_route_executor.c
)

# PR 4: IOCTL + Hook Table + EPT Violation handler
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    hook_manager.c
    ept_violation.c
    ioctl_validate.c
)

# Runtime V2: VMX region contracts and BASIC parsing
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_region.c
)

# Runtime V2: launch context contract helpers
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_runtime_context.c
)

# Runtime V2: processor state validation
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_processor_state.c
)

# Persistent Runtime V3: pure processor-topology snapshot
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_processor_topology.c
)

# Persistent Runtime V3E1: kernel topology and affinity seam
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    ../driver/vmx_persistent_kernel.c
)

# Persistent Runtime V3E3: prepared-context lifecycle operations
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_prepare.c
)

# Persistent Runtime V3E4: pure persistent VMCS plan binder
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_vmcs_plan.c
)

# Persistent Runtime V3C1: per-CPU context contract
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_context.c
)

# Persistent Runtime V3D3a: launch-storage validation/initialization
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_launch_init.c
)
# Persistent Runtime V3C2: lifecycle/context bridge
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_bridge.c
)

# Persistent Runtime V3: aggregate lifecycle
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_lifecycle.c
)

# Runtime V2 controls and validated WDK-free EPT foundation
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_controls.c
    vmx_ept_model.c
    vmx_ept_session_config.c
    vmx_ept_identity_map.c
    vmx_ept_invalidation.c
    vmx_ept_executor.c
    vmx_ept_domain.c
    vmx_ept_storage_owner.c
    vmx_ept_root_coordinator.c
    vmx_ept_invalidation_coverage.c
    vmx_ept_session_owner.c
    vmx_ept_session_operations.c
    vmx_ept_invalidation_coordinator.c
    vmx_ept_installation_coordinator.c
    vmx_ept_root_entry_coordinator.c
    vmx_ept_runtime_root_plan.c
    vmx_ept_runtime_adoption_coordinator.c
    vmx_ept_native_context.c
)

# Runtime V2: VMCS write plan
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmcs_plan.c
)

# Runtime V2: VMCALL protocol
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_protocol.c
)

# Persistent Runtime V3D2a: pure READY/SHUTDOWN protocol
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_protocol.c
)

# Production CPUID transparency route planning.
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_cpuid_route_plan.c
    vmx_msr_read_route_plan.c
    vmx_msr_write_route_plan.c
    vmx_vpid_capability.c
    vmx_vpid_owner.c
    vmx_vpid_invalidation.c
    vmx_vpid_executor.c
    vmx_vpid_vmcs_coordinator.c
)

# Persistent Runtime V3D2b: pure VM-exit dispatcher
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_exit_telemetry.c
    vmx_dispatch_status.c
    vmx_observation_status.c
    vmx_persistent_dispatch.c
)

# Runtime V2: VM-exit dispatcher
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_exit_dispatch.c
)

# Runtime V2: lifecycle model
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_lifecycle.c
)

# Runtime V2: kernel preflight
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_preflight.c
)

# Runtime V2: transient runtime
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_transient.c
)

# G1: IOCTL launch-probe wiring (preflight-to-domain mapping)
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    ioctl_launch_probe.c
)

# Persistent Runtime V3E5b: persistent activation operation
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_activate.c
)
# Persistent Runtime V3E6a: serialized lifecycle coordinator
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_coordinator.c
)
# Persistent Runtime V3E6b: per-CPU operation router
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_operation_router.c
)
# Persistent Runtime V3E6c: kernel-owned resource composition
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_resource.c
)
# Persistent Runtime V3A3: per-CPU operation-platform owner
set(HV_CORE_SOURCES ${HV_CORE_SOURCES}
    vmx_persistent_operation_owner.c
)

# Persistent Runtime V3A4 frozen-topology platform
list(APPEND HV_CORE_SOURCES vmx_persistent_frozen_platform.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_guarded_platform.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_runtime_storage.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_public_status.c)
list(APPEND HV_CORE_SOURCES vmx_ept_persistent_status_model.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_start_diagnostic.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_preentry.c)
list(APPEND HV_CORE_SOURCES vmx_platform_readiness.c)
list(APPEND HV_CORE_SOURCES vmx_persistent_unload_policy.c)
list(APPEND HV_CORE_SOURCES vmx_ept_hook_transaction.c)
list(APPEND HV_CORE_SOURCES vmx_ept_hook_location.c)
list(APPEND HV_CORE_SOURCES vmx_ept_hook_owner.c)

add_library(hypervisor_core STATIC ${HV_CORE_SOURCES})

target_include_directories(hypervisor_core
    PUBLIC "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
)

target_compile_features(hypervisor_core PUBLIC c_std_11)

# Strict compilation — zero warnings policy (compiler-agnostic)
if(MSVC)
    target_compile_options(hypervisor_core PRIVATE
        /W4           # Warning level 4
        /WX           # Treat warnings as errors
        /GS-          # No buffer security checks (VMX code doesn't need them)
        /GR-          # No RTTI
    )
else()
    target_compile_options(hypervisor_core PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
        -fno-rtti
    )
endif()

target_compile_definitions(hypervisor_core PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# ASM files for VMX trampoline (enabled in later PRs when .asm files exist)
# enable_language(ASM_MASM)
