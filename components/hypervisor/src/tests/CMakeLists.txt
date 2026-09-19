# Tests — CTest-driven unit tests for hypervisor_core
# Uses a minimal self-contained test harness (no GTest/Catch2 dependency).
# Tests run in user-mode and link only stateless pure-C logic.

# enable_testing() is called at project root scope

# Test common header (minimal assertion macros)
set(TEST_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/test_common.h")
add_executable(test_vmx_ept_hook_transaction test_vmx_ept_hook_transaction.c)
target_include_directories(test_vmx_ept_hook_transaction PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_hook_transaction PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_hook_transaction PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_hook_transaction PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_hook_transaction PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_hook_transaction COMMAND test_vmx_ept_hook_transaction)


#---------------------------------------------------------------------
# Test: VMX initialization logic
#---------------------------------------------------------------------
add_executable(test_vmx_init
    test_vmx_init.c
)

target_include_directories(test_vmx_init
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_vmx_init
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_init PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_init PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_init PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_init
    COMMAND test_vmx_init
)
#---------------------------------------------------------------------
# Test: Persistent Runtime V3 processor topology snapshot
#---------------------------------------------------------------------
add_executable(test_vmx_processor_topology
    test_vmx_processor_topology.c
)

target_include_directories(test_vmx_processor_topology
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_processor_topology
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_processor_topology PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_processor_topology PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_processor_topology PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_processor_topology
    COMMAND test_vmx_processor_topology
)
#---------------------------------------------------------------------
# Test: Persistent Runtime V3 aggregate lifecycle
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_lifecycle
    test_vmx_persistent_lifecycle.c
)

target_include_directories(test_vmx_persistent_lifecycle
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_persistent_lifecycle
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_persistent_lifecycle PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_lifecycle PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_lifecycle PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_persistent_lifecycle
    COMMAND test_vmx_persistent_lifecycle
)

#---------------------------------------------------------------------
# Test: Persistent Runtime V3C1 per-CPU context
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_context
    test_vmx_persistent_context.c
)

target_include_directories(test_vmx_persistent_context
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_persistent_context
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_persistent_context PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_context PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_context PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_persistent_context
    COMMAND test_vmx_persistent_context
)
#---------------------------------------------------------------------
# Test: Persistent Runtime V3C2 lifecycle/context bridge
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_bridge
    test_vmx_persistent_bridge.c
)
target_include_directories(test_vmx_persistent_bridge
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)
target_link_libraries(test_vmx_persistent_bridge PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_bridge PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_bridge PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_bridge PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_bridge COMMAND test_vmx_persistent_bridge)
#---------------------------------------------------------------------
# Test: Persistent Runtime V3E1 topology and affinity seam
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_kernel
    test_vmx_persistent_kernel.c
)
target_include_directories(test_vmx_persistent_kernel
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)
target_link_libraries(test_vmx_persistent_kernel PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_kernel PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_kernel PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_kernel PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_kernel COMMAND test_vmx_persistent_kernel)

#---------------------------------------------------------------------
# Test: Persistent Runtime V3E3 prepared-context operations
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_prepare test_vmx_persistent_prepare.c)
target_include_directories(test_vmx_persistent_prepare PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_prepare PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_prepare PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_prepare PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_prepare PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_prepare COMMAND test_vmx_persistent_prepare)

#---------------------------------------------------------------------
# Test: Persistent Runtime V3E4 VMCS plan binder
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_vmcs_plan test_vmx_persistent_vmcs_plan.c)
target_include_directories(test_vmx_persistent_vmcs_plan PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_vmcs_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_vmcs_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_vmcs_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_vmcs_plan PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_vmcs_plan COMMAND test_vmx_persistent_vmcs_plan)

#---------------------------------------------------------------------
# Test: Persistent Runtime V3E5 Activate/Stop platform contract
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_activate_contracts test_vmx_persistent_activate_contracts.c)
target_include_directories(test_vmx_persistent_activate_contracts PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_activate_contracts PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_activate_contracts PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_activate_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_activate_contracts PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_activate_contracts COMMAND test_vmx_persistent_activate_contracts)

#---------------------------------------------------------------------
# Test: CPU feature detection logic
#---------------------------------------------------------------------
add_executable(test_cpu_features
    test_cpu_features.c
)

target_include_directories(test_cpu_features
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_cpu_features
    PRIVATE hypervisor_core
)

target_compile_features(test_cpu_features PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_cpu_features PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_cpu_features PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_cpu_features
    COMMAND test_cpu_features
)

#---------------------------------------------------------------------
# Test: EPT skeleton (filled in later PRs)
#---------------------------------------------------------------------
add_executable(test_ept
    test_ept.c
)

target_include_directories(test_ept
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_ept
    PRIVATE hypervisor_core
)

target_compile_features(test_ept PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_ept PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_ept PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_ept
    COMMAND test_ept
)

#---------------------------------------------------------------------
# Test: validated EPT capability and EPTP model
#---------------------------------------------------------------------
add_executable(test_vmx_ept_model
    test_vmx_ept_model.c
)

target_include_directories(test_vmx_ept_model
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_vmx_ept_model PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_model PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_model PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_model PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(NAME test_vmx_ept_model COMMAND test_vmx_ept_model)

#---------------------------------------------------------------------
# Test: caller-owned EPT identity-map population
#---------------------------------------------------------------------
add_executable(test_vmx_ept_identity_map
    test_vmx_ept_identity_map.c
)
target_include_directories(test_vmx_ept_identity_map PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_identity_map PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_identity_map PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_identity_map PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_identity_map PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_identity_map COMMAND test_vmx_ept_identity_map)
#---------------------------------------------------------------------
# Test: pure INVEPT request and descriptor model
#---------------------------------------------------------------------
add_executable(test_vmx_ept_invalidation
    test_vmx_ept_invalidation.c
)
target_include_directories(test_vmx_ept_invalidation PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_invalidation PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_invalidation PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_invalidation PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_invalidation PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_invalidation COMMAND test_vmx_ept_invalidation)

#---------------------------------------------------------------------
# Test: injectable checked INVEPT execution boundary
#---------------------------------------------------------------------
add_executable(test_vmx_ept_executor
    test_vmx_ept_executor.c
)
target_include_directories(test_vmx_ept_executor PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_executor PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_executor PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_executor PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_executor PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_executor COMMAND test_vmx_ept_executor)

#---------------------------------------------------------------------
# Test: allocation-free EPT domain ownership lifecycle
#---------------------------------------------------------------------
add_executable(test_vmx_ept_domain
    test_vmx_ept_domain.c
    test_vmx_ept_domain_cpp.cpp
)
target_include_directories(test_vmx_ept_domain PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_domain PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_domain PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_domain PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_domain PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_domain COMMAND test_vmx_ept_domain)
add_test(NAME test_vmx_ept_domain_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptDomain.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_domain.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_domain.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_domain.c"
)
#---------------------------------------------------------------------
# Test: allocation-owned EPT table/domain storage
#---------------------------------------------------------------------
add_executable(test_vmx_ept_storage_owner
    test_vmx_ept_storage_owner.c
    test_vmx_ept_storage_owner_cpp.cpp
)
target_include_directories(test_vmx_ept_storage_owner PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_storage_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_storage_owner PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_storage_owner PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_storage_owner PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_storage_owner COMMAND test_vmx_ept_storage_owner)
add_test(NAME test_vmx_ept_storage_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptStorageOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_storage_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_storage_owner.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_storage_owner.c"
)
#---------------------------------------------------------------------
#---------------------------------------------------------------------
#---------------------------------------------------------------------
# Test: WDK-free multi-context EPT root coordinator
#---------------------------------------------------------------------
add_executable(test_vmx_ept_root_coordinator
    test_vmx_ept_root_coordinator.c
    test_vmx_ept_root_coordinator_cpp.cpp)
target_include_directories(test_vmx_ept_root_coordinator PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_vmx_ept_root_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_root_coordinator PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_root_coordinator PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_root_coordinator PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_root_coordinator COMMAND test_vmx_ept_root_coordinator)
add_test(NAME test_vmx_ept_root_coordinator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptRootCoordinator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_root_coordinator.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_root_coordinator.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_root_coordinator.c")
#---------------------------------------------------------------------
# Test: WDK-free per-context EPT invalidation coverage
#---------------------------------------------------------------------
add_executable(test_vmx_ept_invalidation_coverage
    test_vmx_ept_invalidation_coverage.c
    test_vmx_ept_invalidation_coverage_cpp.cpp)
target_include_directories(test_vmx_ept_invalidation_coverage PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_vmx_ept_invalidation_coverage PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_invalidation_coverage PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_invalidation_coverage PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_invalidation_coverage PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_invalidation_coverage
    COMMAND test_vmx_ept_invalidation_coverage)
add_test(NAME test_vmx_ept_invalidation_coverage_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInvalidationCoverage.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coverage.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coverage.h"
        -DomainPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_domain.c"
        -RootPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_root_coordinator.c"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")# Test: trusted WDK EPT storage adapter remains isolated
#---------------------------------------------------------------------
add_test(NAME test_vmx_ept_storage_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptStorageAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_storage_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_storage_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c"
)
# Test: WDK-free native EPT adapter context model
#---------------------------------------------------------------------
add_executable(test_vmx_ept_native_context
    test_vmx_ept_native_context.c
    test_vmx_ept_native_context_cpp.cpp
)
target_include_directories(test_vmx_ept_native_context PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_ept_native_context PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_native_context PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_native_context PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_native_context PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_ept_native_context COMMAND test_vmx_ept_native_context)

add_test(NAME test_vmx_ept_native_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptNativeAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.h"
        -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_native_context.h"
        -AsmPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_invept.asm"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/driver/hook_table.c"
)


#---------------------------------------------------------------------
# Test: VM-Exit handler dispatch and fast-path handlers
#---------------------------------------------------------------------
add_executable(test_vmexit
    test_vmexit.c
    ../hypervisor/vmx_vmexit.c
)

target_include_directories(test_vmexit
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmexit
    PRIVATE hypervisor_core
)

target_compile_features(test_vmexit PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmexit PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmexit PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmexit
    COMMAND test_vmexit
)

add_test(
    NAME test_production_legacy_boundary
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestProductionLegacyBoundary.ps1"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -PublicVmxHeaderPath "${CMAKE_SOURCE_DIR}/include/vmx.h"
        -PublicEptHeaderPath "${CMAKE_SOURCE_DIR}/include/ept.h"
        -CpuFeaturesPath "${CMAKE_SOURCE_DIR}/src/hypervisor/cpu_features.c"
        -DriverSourcePath "${CMAKE_SOURCE_DIR}/src/driver"
)

add_test(
    NAME test_production_boundary_release_candidate
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestProductionBoundaryReleaseCandidate.ps1"
        -GatePath "${CMAKE_SOURCE_DIR}/setup/gate63-production-boundary-release-candidate.ps1"
        -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-ProductionBoundaryReleaseCandidate.ps1"
        -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RELEASE_BOUNDARY.md"
        -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
)

#---------------------------------------------------------------------
# Test: Transparency — CPUID/MSR/CR3/CET (PR 3)
#---------------------------------------------------------------------
add_executable(test_transparency
    test_transparency.c
)

target_include_directories(test_transparency
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_transparency
    PRIVATE hypervisor_core
)

target_compile_features(test_transparency PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_transparency PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_transparency PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_transparency
    COMMAND test_transparency
)

#---------------------------------------------------------------------
# Test: IOCTL dispatch, hook table CRUD, EPT violation (PR 4)
#---------------------------------------------------------------------
add_executable(test_ioctl
    test_ioctl.c
)

target_include_directories(test_ioctl
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_ioctl
    PRIVATE hypervisor_core
)

target_compile_features(test_ioctl PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_ioctl PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_ioctl PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_ioctl
    COMMAND test_ioctl
)

#---------------------------------------------------------------------
# Test: VMX contracts — pure helpers and field encoding constants
# Header-only tests (no WDK dependency — helpers are static inline in vmx.h)
#---------------------------------------------------------------------
add_executable(test_vmx_contracts
    test_vmx_contracts.c
)

target_include_directories(test_vmx_contracts
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Header-only (static inline functions) — no library link needed

target_compile_features(test_vmx_contracts PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_contracts PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_contracts
    COMMAND test_vmx_contracts
)

#---------------------------------------------------------------------
# Test: VMX launch C/MASM ABI layout and constants (Runtime V2 Slice B)
#---------------------------------------------------------------------
add_executable(test_vmx_launch_contracts
    test_vmx_launch_contracts.c
)

target_include_directories(test_vmx_launch_contracts
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(test_vmx_launch_contracts PRIVATE c_std_11)

if(MSVC)
    target_compile_options(test_vmx_launch_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_launch_contracts PRIVATE -Wall -Wextra -Werror)
endif()

add_test(
    NAME test_vmx_launch_contracts
    COMMAND test_vmx_launch_contracts
)

#---------------------------------------------------------------------
# Test: Persistent V3D1 private launch C/MASM ABI contract
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_launch_contracts
    test_vmx_persistent_launch_contracts.c
)

target_include_directories(test_vmx_persistent_launch_contracts
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(test_vmx_persistent_launch_contracts PRIVATE c_std_11)

if(MSVC)
    target_compile_options(test_vmx_persistent_launch_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_launch_contracts PRIVATE -Wall -Wextra -Werror)
endif()

add_test(
    NAME test_vmx_persistent_launch_contracts
    COMMAND test_vmx_persistent_launch_contracts
)

#---------------------------------------------------------------------
# Test: Persistent V3D3a launch-storage initializer
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_launch_init
    test_vmx_persistent_launch_init.c
)
target_include_directories(test_vmx_persistent_launch_init
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)
target_link_libraries(test_vmx_persistent_launch_init PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_launch_init PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_launch_init PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_launch_init PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_launch_init COMMAND test_vmx_persistent_launch_init)
# Test: Persistent Runtime V3D2a READY/SHUTDOWN protocol
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_protocol
    test_vmx_persistent_protocol.c
)
target_include_directories(test_vmx_persistent_protocol
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_persistent_protocol PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_protocol PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_protocol PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_protocol PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_protocol COMMAND test_vmx_persistent_protocol)

#---------------------------------------------------------------------
# Test: loader CPUID transparency proof classification
#---------------------------------------------------------------------
add_executable(test_loader_cpuid_probe
    test_loader_cpuid_probe.c
    ../loader/loader_cpuid_probe.c
)
target_include_directories(test_loader_cpuid_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/loader"
)
target_compile_features(test_loader_cpuid_probe PRIVATE c_std_17)
if(MSVC)
    target_compile_options(test_loader_cpuid_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(test_loader_cpuid_probe PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_loader_cpuid_probe COMMAND test_loader_cpuid_probe)

#---------------------------------------------------------------------
# Test: pure CPUID transparency route planning
#---------------------------------------------------------------------
add_executable(test_vmx_cpuid_route_plan
    test_vmx_cpuid_route_plan.c
)
target_include_directories(test_vmx_cpuid_route_plan PRIVATE
    "${HYPERSINV_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor"
)
target_link_libraries(test_vmx_cpuid_route_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_cpuid_route_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_cpuid_route_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cpuid_route_plan PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_cpuid_route_plan COMMAND test_vmx_cpuid_route_plan)

#---------------------------------------------------------------------
# Test: Persistent Runtime V3D2b VM-exit dispatcher
#---------------------------------------------------------------------
add_executable(test_vmx_persistent_dispatch
    test_vmx_persistent_dispatch.c
)
target_include_directories(test_vmx_persistent_dispatch
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_vmx_persistent_dispatch PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_dispatch PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_dispatch PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_dispatch PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_dispatch COMMAND test_vmx_persistent_dispatch)

#---------------------------------------------------------------------
# Test: VMX region contracts — BASIC parsing and region validation (R2)
#---------------------------------------------------------------------
add_executable(test_vmx_region_contracts
    test_vmx_region_contracts.c
)

target_include_directories(test_vmx_region_contracts
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_vmx_region_contracts
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_region_contracts PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_region_contracts PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_region_contracts PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_region_contracts
    COMMAND test_vmx_region_contracts
)

#---------------------------------------------------------------------
# Test: VMX runtime context — layout, helpers, contract (R3a)
#---------------------------------------------------------------------
add_executable(test_vmx_runtime_context
    test_vmx_runtime_context.c
)

target_include_directories(test_vmx_runtime_context
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_runtime_context
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_runtime_context PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_runtime_context PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_runtime_context PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_runtime_context
    COMMAND test_vmx_runtime_context
)

#---------------------------------------------------------------------
# Test: VMX processor state validation (R4a)
#---------------------------------------------------------------------
add_executable(test_vmx_processor_state
    test_vmx_processor_state.c
)

target_include_directories(test_vmx_processor_state
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_processor_state
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_processor_state PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_processor_state PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_processor_state PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_processor_state
    COMMAND test_vmx_processor_state
)

#---------------------------------------------------------------------
# Test: VMX control planning (R5)
#---------------------------------------------------------------------
add_executable(test_vmx_controls
    test_vmx_controls.c
)

target_include_directories(test_vmx_controls
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_controls
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_controls PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_controls PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_controls PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_controls
    COMMAND test_vmx_controls
)

#---------------------------------------------------------------------
# Test: VMCS write plan (R6)
#---------------------------------------------------------------------
add_executable(test_vmcs_plan
    test_vmcs_plan.c
)

target_include_directories(test_vmcs_plan
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmcs_plan
    PRIVATE hypervisor_core
)

target_compile_features(test_vmcs_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmcs_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmcs_plan PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmcs_plan
    COMMAND test_vmcs_plan
)

#---------------------------------------------------------------------
# Test: VMCALL protocol state machine (R10)
#---------------------------------------------------------------------
add_executable(test_vmx_protocol
    test_vmx_protocol.c
)

target_include_directories(test_vmx_protocol
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
)

target_link_libraries(test_vmx_protocol
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_protocol PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_protocol PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_protocol PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_protocol
    COMMAND test_vmx_protocol
)

#---------------------------------------------------------------------
# Test: VM-exit dispatcher (R11)
#---------------------------------------------------------------------
add_executable(test_vmx_exit_dispatch
    test_vmx_exit_dispatch.c
)

target_include_directories(test_vmx_exit_dispatch
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
)

target_link_libraries(test_vmx_exit_dispatch
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_exit_dispatch PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_exit_dispatch PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_exit_dispatch PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_exit_dispatch
    COMMAND test_vmx_exit_dispatch
)

#---------------------------------------------------------------------
# Test: Lifecycle and cleanup model (R12)
#---------------------------------------------------------------------
add_executable(test_vmx_lifecycle
    test_vmx_lifecycle.c
)

target_include_directories(test_vmx_lifecycle
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_link_libraries(test_vmx_lifecycle
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_lifecycle PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_lifecycle PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_lifecycle PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_lifecycle
    COMMAND test_vmx_lifecycle
)

#---------------------------------------------------------------------
# Test: Kernel preflight (R13A)
#---------------------------------------------------------------------
add_executable(test_vmx_preflight
    test_vmx_preflight.c
)

target_include_directories(test_vmx_preflight
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
)

target_link_libraries(test_vmx_preflight
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_preflight PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_preflight PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_preflight PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_preflight
    COMMAND test_vmx_preflight
)

#---------------------------------------------------------------------
# Test: Transient runtime (R13B)
#---------------------------------------------------------------------
add_executable(test_vmx_transient
    test_vmx_transient.c
)

target_include_directories(test_vmx_transient
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
)

target_link_libraries(test_vmx_transient
    PRIVATE hypervisor_core
)

target_compile_features(test_vmx_transient PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_transient PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_transient PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_transient
    COMMAND test_vmx_transient
)

#---------------------------------------------------------------------
# Test: Diagnostic ABI layout (R14)
#---------------------------------------------------------------------
add_executable(test_vmx_diagnostic
    test_vmx_diagnostic.c
)

target_include_directories(test_vmx_diagnostic
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(test_vmx_diagnostic PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_diagnostic PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_diagnostic PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_vmx_diagnostic
    COMMAND test_vmx_diagnostic
)

#---------------------------------------------------------------------
# Test: Loader launch-probe validation and verdict (E1b-ii)
#---------------------------------------------------------------------
add_executable(test_loader_launch_probe
    test_loader_launch_probe.c
    ../loader/launch_probe_diagnostics.c
)

target_include_directories(test_loader_launch_probe
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/loader"
)

target_compile_features(test_loader_launch_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_loader_launch_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_loader_launch_probe PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_loader_launch_probe
    COMMAND test_loader_launch_probe
)

#---------------------------------------------------------------------
# Test: Loader unload/service lifecycle decisions (H1)
#---------------------------------------------------------------------
add_executable(test_loader_service
    test_loader_service.c
    ../loader/loader_service.c
)

target_include_directories(test_loader_service
    PRIVATE "${HYPERSINV_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/loader"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/driver"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/hypervisor"
)

target_compile_features(test_loader_service PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_loader_service PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_loader_service PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

add_test(
    NAME test_loader_service
    COMMAND test_loader_service
)
add_test(NAME test_loader_persistent_classifier_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderPersistentClassifier.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")

add_test(NAME test_loader_ept_status_classifier_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderEptStatusClassifier.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")

add_test(NAME test_loader_cr3_status_query_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderCr3StatusQuery.ps1"
        -MainPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")

add_test(NAME test_loader_cr3_status_classifier_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderCr3StatusClassifier.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -AbiPath "${CMAKE_SOURCE_DIR}/include/vmx_cr3_status_abi.h"
        -PublicSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_public_status.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")

add_test(NAME test_loader_ept_status_proof_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderEptStatusProof.ps1"
        -MainPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -ServicePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")
add_test(NAME test_loader_authoritative_status_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderAuthoritativeStatus.ps1"
        -MainPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -ServicePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")
add_test(NAME test_legacy_core_mgmt_exclusion_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLegacyCoreMgmtExclusion.ps1"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -ReadmePath "${CMAKE_SOURCE_DIR}/README.md")
add_test(NAME test_loader_persistent_load_verification_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderPersistentLoadVerification.ps1"
        -MainPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c" -ServicePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h" -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c")
add_test(NAME test_loader_persistent_cleanup_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestLoaderPersistentCleanup.ps1"
        -MainPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -ServicePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h")
# Persistent Runtime V3E5b activation implementation
add_executable(test_vmx_persistent_activate test_vmx_persistent_activate.c)
target_include_directories(test_vmx_persistent_activate PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_activate PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_activate PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_activate PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_activate PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_activate COMMAND test_vmx_persistent_activate)

# Persistent Runtime V3E5b2a activation failure matrix
add_executable(test_vmx_persistent_activate_failures test_vmx_persistent_activate_failures.c)
target_include_directories(test_vmx_persistent_activate_failures PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_activate_failures PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_activate_failures PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_activate_failures PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_activate_failures PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_activate_failures COMMAND test_vmx_persistent_activate_failures)


# Persistent Runtime V3E5c Stop operation
add_executable(test_vmx_persistent_stop test_vmx_persistent_stop.c)
target_include_directories(test_vmx_persistent_stop PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_stop PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_stop PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_stop PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_stop PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_stop COMMAND test_vmx_persistent_stop)


# Persistent Runtime V3E6a coordinator
add_executable(test_vmx_persistent_coordinator test_vmx_persistent_coordinator.c)
target_include_directories(test_vmx_persistent_coordinator PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_coordinator PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_coordinator PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_coordinator PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_coordinator COMMAND test_vmx_persistent_coordinator)

# Persistent Runtime V3E6b operation router
add_executable(test_vmx_persistent_operation_router test_vmx_persistent_operation_router.c)
target_include_directories(test_vmx_persistent_operation_router PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_operation_router PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_operation_router PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_operation_router PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_operation_router PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_operation_router COMMAND test_vmx_persistent_operation_router)

# Persistent Runtime V3E6c: dynamic resource owner
add_executable(test_vmx_persistent_resource test_vmx_persistent_resource.c)
target_include_directories(test_vmx_persistent_resource PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_resource PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_resource PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_resource PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_resource PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_resource COMMAND test_vmx_persistent_resource)

# WDK serializer source contract (runs without a WDK).
add_test(NAME test_vmx_persistent_serializer_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentSerializer.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_serializer.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_serializer.h")
# Persistent Runtime V3A3 operation-platform owner
add_executable(test_vmx_persistent_operation_owner test_vmx_persistent_operation_owner.c)
target_include_directories(test_vmx_persistent_operation_owner PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_operation_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_operation_owner PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_operation_owner PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_operation_owner PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_operation_owner COMMAND test_vmx_persistent_operation_owner)
add_test(NAME test_vmx_persistent_operation_owner_resolver_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentOperationOwnerResolver.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_operation_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_operation_owner.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_operation_owner.c")

# WDK partial persistent basic-operations contract (runs without a WDK).
add_test(NAME test_vmx_persistent_basic_ops_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentBasicOps.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_basic_ops.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_basic_ops.h"
        -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_basic_context.h")

# WDK-free canonical BasicOps context ABI under strict C11 and C++17.
add_executable(test_vmx_persistent_basic_context
    test_vmx_persistent_basic_context.c
    test_vmx_persistent_basic_context_cpp.cpp)
target_include_directories(test_vmx_persistent_basic_context PRIVATE
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_compile_features(test_vmx_persistent_basic_context PRIVATE c_std_11 cxx_std_17)
set_target_properties(test_vmx_persistent_basic_context PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO)
if(MSVC)
    target_compile_options(test_vmx_persistent_basic_context PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_basic_context PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_basic_context COMMAND test_vmx_persistent_basic_context)

# WDK persistent state/control binder contract (runs without a WDK).
add_test(NAME test_vmx_persistent_state_ops_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentStateOps.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.h")

# WDK persistent ApplyPlan binder contract (runs without a WDK).
add_test(NAME test_vmx_persistent_apply_ops_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentApplyOps.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_apply_ops.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_apply_ops.h")

# WDK persistent VMX/MASM composition contract (runs without a WDK).
add_test(NAME test_vmx_persistent_composition_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentComposition.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.h")
# WDK persistent runtime-storage operation composition (runs without a WDK).
add_test(NAME test_vmx_persistent_runtime_ops_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentRuntimeOps.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.h")

# WDK complete persistent runtime lifecycle (runs without a WDK).
add_test(NAME test_vmx_persistent_runtime_lifecycle_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentRuntimeLifecycle.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h")
add_test(NAME test_vmx_persistent_runtime_root_proof_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentRuntimeRootProof.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
        -ProofHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_root_proof.h")
# WDK Persistent V3 allocator adapter contract (runs without a WDK).
add_test(NAME test_vmx_kernel_persistent_allocator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxKernelPersistentAllocator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_kernel_adapters.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_kernel_adapters.h")

# Persistent Runtime V3A4 frozen-topology platform
add_executable(test_vmx_persistent_frozen_platform test_vmx_persistent_frozen_platform.c)
target_include_directories(test_vmx_persistent_frozen_platform PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_frozen_platform PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_frozen_platform PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_frozen_platform PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_frozen_platform PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_vmx_persistent_frozen_platform COMMAND test_vmx_persistent_frozen_platform)

# Persistent Runtime V3A4 strongly typed runtime storage
add_executable(test_vmx_persistent_runtime_storage test_vmx_persistent_runtime_storage.c)
target_include_directories(test_vmx_persistent_runtime_storage PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_runtime_storage PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_runtime_storage PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_runtime_storage PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_runtime_storage PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_runtime_storage COMMAND test_vmx_persistent_runtime_storage)
# Persistent Runtime V3F1 pointer-free public status encoder
add_executable(test_vmx_persistent_public_status
    test_vmx_persistent_public_status.c test_vmx_persistent_public_status_cpp.cpp)
target_include_directories(test_vmx_persistent_public_status PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_public_status PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_public_status PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_persistent_public_status PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_public_status PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_public_status COMMAND test_vmx_persistent_public_status)
# Private pointer-free Persistent EPT/runtime status model.
add_executable(test_vmx_ept_persistent_status_model
    test_vmx_ept_persistent_status_model.c test_vmx_ept_status_abi_cpp.cpp)
target_include_directories(test_vmx_ept_persistent_status_model PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_persistent_status_model PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_persistent_status_model PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_persistent_status_model PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_persistent_status_model PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_persistent_status_model
    COMMAND test_vmx_ept_persistent_status_model)


# WDK outer persistent driver-owner contract (runs without a WDK).
add_test(NAME test_vmx_persistent_driver_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentDriverOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.h"
        -RuntimeSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -RuntimeHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h")
# KMDF persistent device-owner context contract (runs without a WDK).
add_test(NAME test_vmx_persistent_device_context_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentDeviceContext.ps1"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h")
# V3F3a: pointer-free exact-length persistent status query ABI.
add_executable(test_persistent_status_query
    test_persistent_status_query.c test_persistent_status_query_cpp.cpp)
target_include_directories(test_persistent_status_query PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_persistent_status_query PRIVATE hypervisor_core)
target_compile_features(test_persistent_status_query PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_persistent_status_query PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_persistent_status_query PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_persistent_status_query COMMAND test_persistent_status_query)
add_test(NAME test_persistent_status_query_abi_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestPersistentStatusQueryAbi.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_persistent_status_query.c")

# Fixed read-only 0x808 EPT status query ABI; no driver dispatch wiring here.
add_executable(test_vmx_ept_status_query
    test_vmx_ept_status_query.c test_vmx_ept_status_query_cpp.cpp)
target_include_directories(test_vmx_ept_status_query PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_status_query PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_status_query PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_status_query PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_status_query PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_status_query COMMAND test_vmx_ept_status_query)
add_test(NAME test_vmx_ept_status_query_abi_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptStatusQueryAbi.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_status_query.c")

# Fixed read-only 0x80B CR3 status query ABI; no dispatch wiring here.
add_executable(test_vmx_cr3_status_query
    test_vmx_cr3_status_query.c test_vmx_cr3_status_query_cpp.cpp)
target_include_directories(test_vmx_cr3_status_query PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_status_query PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_status_query PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_cr3_status_query PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cr3_status_query PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_status_query COMMAND test_vmx_cr3_status_query)
add_test(NAME test_vmx_cr3_status_query_abi_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3StatusQueryAbi.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_status_query.c"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")

# Fixed operator-gated 0x80E authenticated non-root CPUID probe ABI.
add_executable(test_vmx_cpuid_probe
    test_vmx_cpuid_probe.c)
target_include_directories(test_vmx_cpuid_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cpuid_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_cpuid_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_cpuid_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cpuid_probe PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cpuid_probe COMMAND test_vmx_cpuid_probe)
# Fixed operator-gated 0x80C one-shot CR3-load probe ABI.
add_executable(test_vmx_cr3_probe
    test_vmx_cr3_probe.c test_vmx_cr3_probe_cpp.cpp)
target_include_directories(test_vmx_cr3_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_probe PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_cr3_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cr3_probe PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_probe COMMAND test_vmx_cr3_probe)
add_test(NAME test_vmx_cr3_probe_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3Probe.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -LaunchPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -AbiTestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_probe.c"
        -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate14-cr3-probe-diagnostic.ps1")
# Fixed operator-gated 0x80D positive CR3 target-residency probe ABI.
add_executable(test_vmx_cr3_residency_probe
    test_vmx_cr3_boundary_roundtrip.c
    test_vmx_cr3_residency_probe_cpp.cpp)
target_include_directories(test_vmx_cr3_residency_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_residency_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_residency_probe PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_cr3_residency_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cr3_residency_probe PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_residency_probe
    COMMAND test_vmx_cr3_residency_probe)
add_test(NAME test_vmx_cr3_residency_probe_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3ResidencyProbe.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -ModulePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_boundary_roundtrip.c"
        -ModuleHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_boundary_roundtrip.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -AbiTestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_boundary_roundtrip.c"
        -LaunchAsmPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
        -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate17-cr3-residency-roundtrip.ps1"
        -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# V3F3b: private KMDF exact persistent-status query bridge.
add_test(NAME test_persistent_status_query_dispatch_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestPersistentStatusQueryDispatch.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")
add_test(NAME test_vmx_ept_status_query_dispatch_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptStatusQueryDispatch.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c")
add_test(NAME test_vmx_cr3_status_query_dispatch_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3StatusQueryDispatch.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")
# WDK Persistent V3 trusted runtime configuration contract.
add_test(NAME test_vmx_persistent_runtime_config_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentRuntimeConfig.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_config.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_config.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt")

# Persistent Runtime V3 per-target pre-entry safety gate.
add_executable(test_vmx_persistent_preentry test_vmx_persistent_preentry.c)
target_include_directories(test_vmx_persistent_preentry PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_preentry PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_preentry PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_preentry PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_preentry PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_preentry COMMAND test_vmx_persistent_preentry)

# Authorized Persistent V3 LOAD/UNLOAD and transient-probe occupancy contract.
add_test(NAME test_persistent_lifecycle_dispatch_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestPersistentLifecycleDispatch.ps1"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -OwnerSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
        -OwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.h")

# V3G pure/static pre-live Persistent V3 integration gate.
add_executable(test_vmx_persistent_pre_live_gate
    test_vmx_persistent_pre_live_gate.c ../loader/loader_service.c)
target_include_directories(test_vmx_persistent_pre_live_gate PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/loader" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_compile_features(test_vmx_persistent_pre_live_gate PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_pre_live_gate PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_pre_live_gate PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_pre_live_gate COMMAND test_vmx_persistent_pre_live_gate)
add_test(NAME test_vmx_persistent_pre_live_gate_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentPreLiveGate.ps1"
        -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c" -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c" -ServicePath "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -PublicHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_public_status.h" -PublicSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_public_status.c"
        -EptHeaderPath "${HYPERSINV_INCLUDE_DIR}/vmx_ept_status_abi.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_pre_live_gate.c")
# Authorized bounded DriverEvtUnload retry/fail-stop policy.
add_executable(test_vmx_persistent_unload_policy
    test_vmx_persistent_unload_policy.c)
target_include_directories(test_vmx_persistent_unload_policy PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_unload_policy PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_unload_policy PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_unload_policy PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_unload_policy PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_unload_policy COMMAND test_vmx_persistent_unload_policy)
add_test(NAME test_vmx_persistent_driver_unload_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentDriverUnload.ps1"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -PolicySourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_unload_policy.c"
        -PolicyHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_unload_policy.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_unload_policy.c")

# Persistent Start pre-rollback diagnostic capture.
add_executable(test_vmx_persistent_start_diagnostic test_vmx_persistent_start_diagnostic.c)
target_include_directories(test_vmx_persistent_start_diagnostic PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_persistent_start_diagnostic PRIVATE hypervisor_core)
target_compile_features(test_vmx_persistent_start_diagnostic PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_persistent_start_diagnostic PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_persistent_start_diagnostic PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_persistent_start_diagnostic COMMAND test_vmx_persistent_start_diagnostic)

add_test(NAME test_persistent_start_diagnostic_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestPersistentStartDiagnostic.ps1"
        -DiagnosticSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_start_diagnostic.c"
        -DiagnosticHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_start_diagnostic.h"
        -CoordinatorSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_coordinator.c"
        -CoordinatorHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_coordinator.h"
        -CoordinatorTestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_coordinator.c"
        -ResourceSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_resource.c"
        -RuntimeSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -OwnerSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_start_diagnostic.c")

add_test(NAME test_persistent_start_rollback_evidence_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestPersistentStartRollbackEvidence.ps1"
        -PrepareSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_prepare.c"
        -ActivateSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -PrepareTestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_prepare.c"
        -StopTestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_stop.c")
add_test(NAME test_vmx_persistent_affinity_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentAffinityAdapter.ps1"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_kernel.h"
        -KernelSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_kernel.c"
        -AdapterSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_kernel_adapters.c"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_kernel.c")
# EPT-D4a: allocation-owned D2/D3a/D3b session composition.
add_executable(test_vmx_ept_session_owner
    test_vmx_ept_session_owner.c test_vmx_ept_session_owner_cpp.cpp)
target_include_directories(test_vmx_ept_session_owner PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_session_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_session_owner PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_session_owner PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_session_owner PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_session_owner COMMAND test_vmx_ept_session_owner)
add_test(NAME test_vmx_ept_session_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptSessionOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_owner.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_session_owner.c"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_native_context.c")

# EPT-D4b: trusted WDK session construction adapter.
add_test(NAME test_vmx_ept_session_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptSessionAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_session_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_session_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")

# EPT-D4c: WDK-free trusted session configuration policy.
add_executable(test_vmx_ept_session_config
    test_vmx_ept_session_config.c test_vmx_ept_session_config_cpp.cpp)
target_include_directories(test_vmx_ept_session_config PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_session_config PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_session_config PRIVATE c_std_11 cxx_std_17)
if(MSVC)
    target_compile_options(test_vmx_ept_session_config PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_session_config PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_session_config COMMAND test_vmx_ept_session_config)
add_test(NAME test_vmx_ept_session_config_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptSessionConfig.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_config.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_config.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_session_config.c"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")

# EPT-D4d: trusted WDK hardware/range configuration capture.
add_test(NAME test_vmx_ept_session_config_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptSessionConfigAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_session_config_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_session_config_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")

# EPT-D4e: trusted configured-session composition.
add_test(NAME test_vmx_ept_configured_session_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptConfiguredSessionAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_configured_session_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_configured_session_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")
# EPT-E1a: authenticated WDK-free session-operation facade.
add_executable(test_vmx_ept_session_operations
    test_vmx_ept_session_operations.c)
target_include_directories(test_vmx_ept_session_operations PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_session_operations PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_session_operations PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_session_operations PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_session_operations PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_session_operations
    COMMAND test_vmx_ept_session_operations)
add_test(NAME test_vmx_ept_session_operations_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptSessionOperations.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_session_operations.c"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")
# EPT-E2a: WDK-free authenticated invalidation execution coordinator.
add_executable(test_vmx_ept_invalidation_coordinator
    test_vmx_ept_invalidation_coordinator.c)
target_include_directories(test_vmx_ept_invalidation_coordinator PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_invalidation_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_invalidation_coordinator PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_invalidation_coordinator PRIVATE
        /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_invalidation_coordinator PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_invalidation_coordinator
    COMMAND test_vmx_ept_invalidation_coordinator)
add_test(NAME test_vmx_ept_invalidation_coordinator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInvalidationCoordinator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coordinator.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coordinator.h"
        -OperationsSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.c"
        -OperationsHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_invalidation_coordinator.c"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c"
        -NativePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c")

# EPT-E2b: trusted WDK native invalidation binding (static only).
add_test(NAME test_vmx_ept_invalidation_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInvalidationAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_invalidation_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_invalidation_adapter.h"
        -NativeSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.c"
        -NativeHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_native_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E3a: WDK-free checked one-shot EPTP installation coordinator.
add_executable(test_vmx_ept_installation_coordinator
    test_vmx_ept_installation_coordinator.c)
target_include_directories(test_vmx_ept_installation_coordinator PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_installation_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_installation_coordinator PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_installation_coordinator PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_installation_coordinator PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_installation_coordinator
    COMMAND test_vmx_ept_installation_coordinator)
add_test(NAME test_vmx_ept_installation_coordinator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationCoordinator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_installation_coordinator.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_installation_coordinator.h"
        -OperationsSourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.c"
        -OperationsHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_session_operations.h"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_installation_coordinator.c")

# EPT-E3b: trusted WDK native EPTP installation binding (static only).
add_test(NAME test_vmx_ept_installation_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
# EPT-E4a: allocation-owned per-processor native/installation binding graph.
add_test(NAME test_vmx_ept_installation_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E4b: checked root-lifecycle bookkeeping around external VMXON/VMXOFF.
add_test(NAME test_vmx_ept_installation_lifecycle_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationLifecycle.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.h"
        -OwnerSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.c"
        -OwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E4c: checked E4a owner to trusted E3b/E3a installation facade.
add_test(NAME test_vmx_ept_installation_execution_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationExecution.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_execution.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_execution.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E4d: checked E4a owner to trusted E2b/E2a invalidation facade.
add_test(NAME test_vmx_ept_invalidation_execution_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInvalidationExecution.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_invalidation_execution.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_invalidation_execution.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
        -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HookPath "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5a: WDK-free root-entry transaction and rollback coordinator.
add_executable(test_vmx_ept_root_entry_coordinator
    test_vmx_ept_root_entry_coordinator.c)
target_include_directories(test_vmx_ept_root_entry_coordinator PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_root_entry_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_root_entry_coordinator PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_root_entry_coordinator PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_root_entry_coordinator PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_root_entry_coordinator
    COMMAND test_vmx_ept_root_entry_coordinator)
add_test(NAME test_vmx_ept_root_entry_coordinator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptRootEntryCoordinator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_root_entry_coordinator.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_root_entry_coordinator.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_root_entry_coordinator.c"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.c")

# EPT-E5b2: WDK-free runtime-root proof classification and adoption plan.
add_executable(test_vmx_ept_runtime_root_plan
    test_vmx_ept_runtime_root_plan.c)
target_include_directories(test_vmx_ept_runtime_root_plan PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_SOURCE_DIR}/src/driver")
target_link_libraries(test_vmx_ept_runtime_root_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_runtime_root_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_runtime_root_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_runtime_root_plan PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_runtime_root_plan
    COMMAND test_vmx_ept_runtime_root_plan)
add_test(NAME test_vmx_ept_runtime_root_plan_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptRuntimeRootPlan.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_runtime_root_plan.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_runtime_root_plan.h"
        -ProofHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_root_proof.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_runtime_root_plan.c")

add_executable(test_vmx_ept_runtime_adoption_coordinator
    test_vmx_ept_runtime_adoption_coordinator.c)
target_include_directories(test_vmx_ept_runtime_adoption_coordinator PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor" "${CMAKE_SOURCE_DIR}/src/driver")
target_link_libraries(test_vmx_ept_runtime_adoption_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_runtime_adoption_coordinator PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_runtime_adoption_coordinator PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_runtime_adoption_coordinator PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_runtime_adoption_coordinator COMMAND test_vmx_ept_runtime_adoption_coordinator)
add_test(NAME test_vmx_ept_runtime_adoption_coordinator_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptRuntimeAdoptionCoordinator.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_runtime_adoption_coordinator.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_runtime_adoption_coordinator.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_runtime_adoption_coordinator.c"
        -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
add_test(NAME test_vmx_persistent_runtime_root_lease_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentRuntimeRootLease.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
# EPT-E5b4b1: adoption-specific E4b bookkeeping without VMXON/VMXOFF.
add_test(NAME test_vmx_ept_installation_adoption_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptInstallationAdoption.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_lifecycle.h"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
# EPT-E5b4b2: retained runtime lease to exact-CPU E4b adoption adapter.
add_test(NAME test_vmx_ept_runtime_adoption_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptRuntimeAdoptionAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_runtime_adoption_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_runtime_adoption_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
# EPT-E5b5: private configured-session and installation-owner composition.
add_test(NAME test_vmx_ept_composition_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptCompositionOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_composition_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_composition_owner.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_runtime_adoption_adapter.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b6: authenticated Persistent V3 VMCS-binding visitor capture.
add_test(NAME test_vmx_persistent_vmcs_binding_capture_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentVmcsBindingCapture.ps1"
        -RuntimeSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -RuntimeHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
        -OwnerSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
        -OwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.h"
        -BindingHeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_binding.h"
        -EptOwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.h"
        -EptOwnerSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_installation_owner.c"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_composition_owner.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_runtime_adoption_adapter.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b7: private Persistent binding capture to E5b5 initialization.
add_test(NAME test_vmx_ept_persistent_composition_adapter_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentCompositionAdapter.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_composition_adapter.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_composition_adapter.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h,${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_runtime_adoption_adapter.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b8: composition-first ownership and Persistent detach retry.
add_test(NAME test_vmx_ept_persistent_lifecycle_owner_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentLifecycleOwner.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_lifecycle_owner.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_lifecycle_owner.h"
        -CompositionSourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_composition_owner.c"
        -CompositionHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_composition_owner.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b9c: typed-device storage adapter for E5b8 lifecycle calls.
add_test(NAME test_vmx_ept_persistent_device_lifecycle_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentDeviceLifecycle.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_lifecycle.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_lifecycle.h"
        -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b9d1: private ordered device teardown coordinator.
add_test(NAME test_vmx_ept_persistent_device_teardown_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentDeviceTeardown.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b9d2: private ordered device startup coordinator.
add_test(NAME test_vmx_ept_persistent_device_start_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentDeviceStart.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_start.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_start.h"
        -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c,${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")

# EPT-E5b9e2: private authenticated device status projection.
add_test(NAME test_vmx_ept_persistent_device_status_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptPersistentDeviceStatus.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_status.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_status.h"
        -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
        -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
        -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/hook_manager.c")
# Supervised operator-only Runtime+EPT VM lifecycle gate. The test validates the
# script contract but never loads a driver or mutates SCM state.
add_test(NAME test_gate5_ept_lifecycle_script_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestGate5EptLifecycleScript.ps1"
        -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate5-ept-lifecycle.ps1"
        -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Operator-only repetition wrappers. This validates script composition and
# fail-closed evidence behavior without invoking the loader, SCM, or VMX.
add_test(NAME test_gate_ept_lifecycle_repetition_script_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestGateEptLifecycleRepetitionScripts.ps1"
        -Gate6Path "${CMAKE_SOURCE_DIR}/setup/gate6-ept-10cycles.ps1"
        -Gate7Path "${CMAKE_SOURCE_DIR}/setup/gate7-ept-100cycles.ps1"
        -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

add_test(NAME test_vmx_ept_hook_transaction_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookTransaction.ps1"
        -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_transaction.c"
        -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_transaction.h"
        -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_hook_transaction.c")

add_executable(test_vmx_ept_hook_location test_vmx_ept_hook_location.c)
target_include_directories(test_vmx_ept_hook_location PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_hook_location PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_hook_location PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_ept_hook_location PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_ept_hook_location PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_hook_location COMMAND test_vmx_ept_hook_location)

add_test(NAME test_vmx_ept_hook_location_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookLocation.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_location.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_location.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_hook_location.c")

add_executable(test_vmx_ept_hook_owner test_vmx_ept_hook_owner.c)
target_include_directories(test_vmx_ept_hook_owner PRIVATE "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_hook_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_hook_owner PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_ept_hook_owner PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_ept_hook_owner PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_hook_owner COMMAND test_vmx_ept_hook_owner)

add_test(NAME test_vmx_ept_hook_owner_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookOwner.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_owner.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_hook_owner.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_hook_owner.c")
add_test(NAME test_vmx_ept_hook_lifecycle_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookLifecycle.ps1"
 -LifecycleSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_lifecycle.c"
 -LifecycleHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_lifecycle.h"
 -DeviceSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_hooks.c"
 -DeviceHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_hooks.h"
 -ContextHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h"
 -TeardownSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.c"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_vmx_ept_hook_dispatch_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookDispatch.ps1"
 -IoctlSource "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -ValidatorSource "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -LoaderSource "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -TestSource "${CMAKE_SOURCE_DIR}/src/tests/test_ioctl.c")
add_executable(test_vmx_ept_hook_roundtrip_abi
 test_vmx_ept_hook_roundtrip_abi.c
 test_vmx_ept_hook_roundtrip_abi_cpp.cpp)
target_include_directories(test_vmx_ept_hook_roundtrip_abi PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_hook_roundtrip_abi PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_hook_roundtrip_abi PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_ept_hook_roundtrip_abi PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_ept_hook_roundtrip_abi PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_hook_roundtrip_abi COMMAND test_vmx_ept_hook_roundtrip_abi)
add_test(NAME test_vmx_ept_hook_roundtrip_abi_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookRoundtripAbi.ps1"
 -Header "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -Source "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -Test "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_hook_roundtrip_abi.c")


add_test(NAME test_vmx_ept_hook_roundtrip_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookRoundtrip.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_roundtrip.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_roundtrip.h"
 -ContextHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h"
 -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -TeardownPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.c"
 -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")


add_test(NAME test_vmx_ept_hook_roundtrip_dispatch_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookRoundtripDispatch.ps1"
 -IoctlSource "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -RoundtripHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_roundtrip.h"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")


add_test(NAME test_vmx_ept_hook_roundtrip_loader_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptHookRoundtripLoader.ps1"
 -LoaderSource "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")


add_test(NAME test_gate8_ept_hook_roundtrip_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate8EptHookRoundtripScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate8-ept-hook-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate8_hook_add_failure_diagnostics
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate8HookAddDiagnostics.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_hooks.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate9_ept_hook_roundtrip_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate9EptHookRoundtrip10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate9-ept-hook-roundtrip-10cycles.ps1"
 -Gate8Path "${CMAKE_SOURCE_DIR}/setup/gate8-ept-hook-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate10_ept_hook_roundtrip_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate10EptHookRoundtrip100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate10-ept-hook-roundtrip-100cycles.ps1"
 -Gate8Path "${CMAKE_SOURCE_DIR}/setup/gate8-ept-hook-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
# Gate11: fixed ABI and end-to-end static contracts for the supervised one-shot
# EPT-violation VM-exit route. These tests never load the driver or execute VMX.
add_executable(test_vmx_ept_violation_roundtrip_abi
 test_vmx_ept_violation_roundtrip_abi.c
 test_vmx_ept_violation_roundtrip_abi_cpp.cpp)
target_include_directories(test_vmx_ept_violation_roundtrip_abi PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_ept_violation_roundtrip_abi PRIVATE hypervisor_core)
target_compile_features(test_vmx_ept_violation_roundtrip_abi PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_ept_violation_roundtrip_abi PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_ept_violation_roundtrip_abi PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_ept_violation_roundtrip_abi COMMAND test_vmx_ept_violation_roundtrip_abi)

add_test(NAME test_vmx_ept_violation_roundtrip_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptViolationRoundtrip.ps1"
 -Header "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -Validator "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -Dispatch "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -DispatchTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_dispatch.c"
 -RuntimeStorage "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_runtime_storage.h"
 -RuntimeOps "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c"
 -RuntimeLifecycle "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -Owner "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -Start "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_start.c"
 -Roundtrip "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_roundtrip.c"
 -Ioctl "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -Loader "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -AbiTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_ept_violation_roundtrip_abi.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate11_ept_violation_roundtrip_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate11EptViolationRoundtripScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate11-ept-violation-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate12_ept_violation_roundtrip_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate12EptViolationRoundtrip10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate12-ept-violation-roundtrip-10cycles.ps1"
 -Gate11Path "${CMAKE_SOURCE_DIR}/setup/gate11-ept-violation-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate13_ept_violation_roundtrip_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate13EptViolationRoundtrip100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate13-ept-violation-roundtrip-100cycles.ps1"
 -Gate11Path "${CMAKE_SOURCE_DIR}/setup/gate11-ept-violation-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate14_cr3_status_observation_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate14Cr3StatusObservationScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate14-cr3-status-observation.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate15_cr3_probe_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate15Cr3Probe10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate15-cr3-probe-10cycles.ps1"
 -Gate14Path "${CMAKE_SOURCE_DIR}/setup/gate14-cr3-probe-diagnostic.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate16_cr3_probe_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate16Cr3Probe100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate16-cr3-probe-100cycles.ps1"
 -Gate14Path "${CMAKE_SOURCE_DIR}/setup/gate14-cr3-probe-diagnostic.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate18_cr3_residency_roundtrip_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate18Cr3ResidencyRoundtrip10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate18-cr3-residency-roundtrip-10cycles.ps1"
 -Gate17Path "${CMAKE_SOURCE_DIR}/setup/gate17-cr3-residency-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate19_cr3_residency_roundtrip_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate19Cr3ResidencyRoundtrip100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate19-cr3-residency-roundtrip-100cycles.ps1"
 -Gate17Path "${CMAKE_SOURCE_DIR}/setup/gate17-cr3-residency-roundtrip.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
# WDK-free CR3 target selection consumed through the authenticated route owner.
add_executable(test_vmx_cr3_target_selection
 test_vmx_cr3_target_selection.c
 test_vmx_cr3_target_selection_cpp.cpp)
target_include_directories(test_vmx_cr3_target_selection PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_target_selection PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_target_selection PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_target_selection PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_target_selection PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_target_selection COMMAND test_vmx_cr3_target_selection)

add_test(NAME test_vmx_cr3_target_selection_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3TargetSelection.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_target_selection.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_target_selection.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_target_selection.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c,${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")
# Pure CR3 MOV-to decode behind the authenticated route owner.
add_executable(test_vmx_cr3_exit_decode
 test_vmx_cr3_exit_decode.c
 test_vmx_cr3_exit_decode_cpp.cpp)
target_include_directories(test_vmx_cr3_exit_decode PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_exit_decode PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_exit_decode PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_exit_decode PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_exit_decode PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_exit_decode COMMAND test_vmx_cr3_exit_decode)

add_test(NAME test_vmx_cr3_exit_decode_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3ExitDecode.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_exit_decode.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_exit_decode.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_exit_decode.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")

# Pure per-vCPU CR3 target-residency transitions with isolated publication.
add_executable(test_vmx_cr3_residency
 test_vmx_cr3_residency.c
 test_vmx_cr3_residency_cpp.cpp)
target_include_directories(test_vmx_cr3_residency PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_residency PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_residency PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_residency PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_residency PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_residency COMMAND test_vmx_cr3_residency)
add_test(NAME test_vmx_cr3_residency_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3Residency.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_residency.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_residency.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_residency.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")

# Pure CR3 route plan consumed by the authenticated executor.
add_executable(test_vmx_cr3_route_plan
 test_vmx_cr3_route_plan.c test_vmx_cr3_route_plan_cpp.cpp)
target_include_directories(test_vmx_cr3_route_plan PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_route_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_route_plan PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_route_plan PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_route_plan PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_route_plan COMMAND test_vmx_cr3_route_plan)
add_test(NAME test_vmx_cr3_route_plan_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RoutePlan.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_plan.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_plan.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_route_plan.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")
# Pure CR3 execution-control policy behind authenticated runtime activation.
add_executable(test_vmx_cr3_control_policy
 test_vmx_cr3_control_policy.c test_vmx_cr3_control_policy_cpp.cpp)
target_include_directories(test_vmx_cr3_control_policy PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_control_policy PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_control_policy PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_control_policy PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_control_policy PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_control_policy COMMAND test_vmx_cr3_control_policy)
add_test(NAME test_vmx_cr3_control_policy_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3ControlPolicy.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_control_policy.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_control_policy.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_control_policy.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")
# Pure per-CPU CR3 route owner; dispatcher-bound, runtime-unbound.
add_executable(test_vmx_cr3_route_owner
 test_vmx_cr3_route_owner.c test_vmx_cr3_route_owner_cpp.cpp)
target_include_directories(test_vmx_cr3_route_owner PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_route_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_route_owner PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_route_owner PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_route_owner PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_route_owner COMMAND test_vmx_cr3_route_owner)
add_test(NAME test_vmx_cr3_route_owner_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RouteOwner.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_owner.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_owner.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_route_owner.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_runtime_storage.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Pure CR3 route executor bound only through the persistent dispatcher route.
add_executable(test_vmx_cr3_route_executor
 test_vmx_cr3_route_executor.c test_vmx_cr3_route_executor_cpp.cpp)
target_include_directories(test_vmx_cr3_route_executor PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_route_executor PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_route_executor PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_route_executor PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_route_executor PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_route_executor COMMAND test_vmx_cr3_route_executor)
add_test(NAME test_vmx_cr3_route_executor_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RouteExecutor.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_executor.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_executor.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_route_executor.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c,${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Persistent CR3-load dispatcher route behind authenticated runtime binding.
add_test(NAME test_vmx_persistent_cr3_dispatch_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentCr3Dispatch.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_dispatch.c"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c,${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Owned zero-before-bind per-CPU CR3 residency storage.
add_test(NAME test_vmx_cr3_runtime_storage_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RuntimeStorage.ps1"
 -StorageSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_runtime_storage.c"
 -StorageHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_runtime_storage.h"
 -RuntimeOps "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c"
 -RuntimeLifecycle "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_runtime_storage.c"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c,${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c,${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c,${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Authenticated CR3 target copy and per-CPU runtime route binding.
add_test(NAME test_vmx_cr3_runtime_bind_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RuntimeBind.ps1"
 -OpsSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c"
 -OpsHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.h"
 -RuntimeSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -RuntimeHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
 -OwnerSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -OwnerHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.h"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
# Authenticated CR3 route flag gates load-exit controls and target-count zero.
add_test(NAME test_vmx_cr3_control_activation_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3ControlActivation.ps1"
 -ContextHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_basic_context.h"
 -BasicSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_basic_ops.c"
 -ApplySource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_apply_ops.c"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -StateSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c"
 -OpsSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_ops.c"
 -LifecycleSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -PlanSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c"
 -ContextTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_basic_context.c"
 -PlanTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_vmcs_plan.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

# PASSIVE-only capture of the stable System-process CR3 target.
add_test(NAME test_vmx_cr3_target_provider_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File ${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3TargetProvider.ps1
 -SourcePath ${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_target_provider.c
 -HeaderPath ${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_target_provider.h
 -DriverCMakePath ${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt)

# Pointer-free CR3 routing status encoder; roots remain redacted.
add_executable(test_vmx_cr3_public_status
 test_vmx_cr3_public_status.c test_vmx_cr3_status_abi_cpp.cpp)
target_include_directories(test_vmx_cr3_public_status PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_cr3_public_status PRIVATE hypervisor_core)
target_compile_features(test_vmx_cr3_public_status PRIVATE c_std_11 cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_cr3_public_status PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_cr3_public_status PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cr3_public_status COMMAND test_vmx_cr3_public_status)
add_test(NAME test_vmx_cr3_public_status_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3PublicStatus.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_public_status.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_public_status.h"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/vmx_cr3_status_abi.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cr3_public_status.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")
# Private OperationBusy-protected CR3 snapshot encoder; no public dispatch.
add_test(NAME test_vmx_cr3_runtime_status_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3RuntimeStatus.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
 -OwnerSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_owner.c"
 -TargetHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_target_selection.h"
 -PublicSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_public_status.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c,${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")
# Owner-lock to RuntimeBusy CR3 snapshot; consumption is confined to the query bridge.
add_test(NAME test_vmx_cr3_driver_status_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCr3DriverStatus.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.h"
 -RuntimeSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -RuntimeHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.h"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ForbiddenPaths "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c")

add_test(NAME test_vmx_persistent_cpuid_route_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentCpuidRoute.ps1"
 -PlanHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cpuid_route_plan.h"
 -PlanSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cpuid_route_plan.c"
 -DispatchHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.h"
 -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -LoaderClassifier "${CMAKE_SOURCE_DIR}/src/loader/loader_cpuid_probe.c"
 -DispatchTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_dispatch.c"
 -PlanTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cpuid_route_plan.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_vmx_persistent_cpuid_guest_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentCpuidGuestBoundary.ps1"
 -AbiHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -ExitAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -ActivateSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
 -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate20-cpuid-transparency.ps1"
 -WireTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_cpuid_probe.c"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate20_cpuid_transparency_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate20CpuidTransparencyScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate20-cpuid-transparency.ps1"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate21_cpuid_transparency_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate21CpuidTransparency10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate21-cpuid-transparency-10cycles.ps1"
 -Gate20Path "${CMAKE_SOURCE_DIR}/setup/gate20-cpuid-transparency.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")
add_test(NAME test_gate22_cpuid_transparency_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate22CpuidTransparency100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate22-cpuid-transparency-100cycles.ps1"
 -Gate20Path "${CMAKE_SOURCE_DIR}/setup/gate20-cpuid-transparency.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

# Fail-closed policy-owned RDMSR route planning.
add_executable(test_vmx_msr_read_route_plan
    test_vmx_msr_read_route_plan.c)
target_include_directories(test_vmx_msr_read_route_plan PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_msr_read_route_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_msr_read_route_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_msr_read_route_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_msr_read_route_plan PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_msr_read_route_plan COMMAND test_vmx_msr_read_route_plan)

# Pure fail-closed policy-owned WRMSR discard route plan.
add_executable(test_vmx_msr_write_route_plan
    test_vmx_msr_write_route_plan.c)
target_include_directories(test_vmx_msr_write_route_plan PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_msr_write_route_plan PRIVATE hypervisor_core)
target_compile_features(test_vmx_msr_write_route_plan PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_msr_write_route_plan PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_msr_write_route_plan PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_msr_write_route_plan COMMAND test_vmx_msr_write_route_plan)

# Selective MSR-bitmap policy: intercept owned surfaces, pass native state.
add_executable(test_vmx_msr_bitmap_policy
    test_vmx_msr_bitmap_policy.c)
target_include_directories(test_vmx_msr_bitmap_policy PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_msr_bitmap_policy PRIVATE hypervisor_core)
target_compile_features(test_vmx_msr_bitmap_policy PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_msr_bitmap_policy PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_msr_bitmap_policy PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_msr_bitmap_policy COMMAND test_vmx_msr_bitmap_policy)

add_test(NAME test_vmx_persistent_msr_bitmap_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentMsrBitmap.ps1"
        -PolicyHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_bitmap_policy.h"
        -PolicySource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_bitmap_policy.c"
        -PolicyTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_msr_bitmap_policy.c"
        -ContextHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_context.h"
        -ContextSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_context.c"
        -ContextTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_context.c"
        -ControlsHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.h"
        -ControlsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c"
        -ControlsTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_controls.c"
        -StateOpsSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c"
        -KernelAdaptersSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_kernel_adapters.c"
        -VmcsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c"
        -VmcsTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_vmcs_plan.c"
        -LoaderHeader "${CMAKE_SOURCE_DIR}/src/loader/loader_service.h"
        -LoaderSource "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
        -LoaderTest "${CMAKE_SOURCE_DIR}/src/tests/test_loader_service.c"
        -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
        -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

# Gate23 policy-owned RDMSR transparency classifier.
add_executable(test_loader_msr_read_probe
    test_loader_msr_read_probe.c
    ../loader/loader_msr_read_probe.c)
target_include_directories(test_loader_msr_read_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/loader")
target_compile_features(test_loader_msr_read_probe PRIVATE c_std_17)
if(MSVC)
    target_compile_options(test_loader_msr_read_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(test_loader_msr_read_probe PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_loader_msr_read_probe COMMAND test_loader_msr_read_probe)

# Fixed operator-gated 0x80F authenticated non-root RDMSR probe ABI.
add_executable(test_vmx_msr_read_probe
    test_vmx_msr_read_probe.c)
target_include_directories(test_vmx_msr_read_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_msr_read_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_msr_read_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_msr_read_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_msr_read_probe PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_msr_read_probe COMMAND test_vmx_msr_read_probe)

# Gate30 authenticated native RDTSC/live-VMCS transparency proof.
add_executable(test_loader_tsc_probe
    test_loader_tsc_probe.c
    ../loader/loader_tsc_probe.c)
target_include_directories(test_loader_tsc_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/loader"
    "${HYPERSINV_INCLUDE_DIR}")
target_compile_features(test_loader_tsc_probe PRIVATE c_std_17)
if(MSVC)
    target_compile_options(test_loader_tsc_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(test_loader_tsc_probe PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_loader_tsc_probe COMMAND test_loader_tsc_probe)

add_executable(test_vmx_tsc_probe test_vmx_tsc_probe.c)
target_include_directories(test_vmx_tsc_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_SOURCE_DIR}/src/driver")
target_link_libraries(test_vmx_tsc_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_tsc_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_tsc_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_tsc_probe PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_tsc_probe COMMAND test_vmx_tsc_probe)

# Gate31 authenticated native INVLPG/live-primary-control transparency proof.
add_executable(test_loader_invlpg_probe
    test_loader_invlpg_probe.c
    ../loader/loader_invlpg_probe.c)
target_include_directories(test_loader_invlpg_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/loader"
    "${HYPERSINV_INCLUDE_DIR}")
target_compile_features(test_loader_invlpg_probe PRIVATE c_std_17)
if(MSVC)
    target_compile_options(test_loader_invlpg_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(test_loader_invlpg_probe PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_loader_invlpg_probe COMMAND test_loader_invlpg_probe)

add_executable(test_vmx_invlpg_probe test_vmx_invlpg_probe.c)
target_include_directories(test_vmx_invlpg_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_SOURCE_DIR}/src/driver")
target_link_libraries(test_vmx_invlpg_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_invlpg_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_invlpg_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_invlpg_probe PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_invlpg_probe COMMAND test_vmx_invlpg_probe)

# Gate32 authenticated live CET/HLT VMCS-state suppression proof.
add_executable(test_loader_cet_vmcs_probe
    test_loader_cet_vmcs_probe.c
    ../loader/loader_cet_vmcs_probe.c)
target_include_directories(test_loader_cet_vmcs_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/loader"
    "${HYPERSINV_INCLUDE_DIR}")
target_compile_features(test_loader_cet_vmcs_probe PRIVATE c_std_17)
if(MSVC)
    target_compile_options(test_loader_cet_vmcs_probe PRIVATE /W4 /WX /GS)
else()
    target_compile_options(test_loader_cet_vmcs_probe PRIVATE
        -Wall -Wextra -Werror -Wno-unused-parameter)
endif()
add_test(NAME test_loader_cet_vmcs_probe COMMAND test_loader_cet_vmcs_probe)

add_executable(test_vmx_cet_vmcs_probe test_vmx_cet_vmcs_probe.c)
target_include_directories(test_vmx_cet_vmcs_probe PRIVATE
    "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor"
    "${CMAKE_SOURCE_DIR}/src/driver")
target_link_libraries(test_vmx_cet_vmcs_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_cet_vmcs_probe PRIVATE c_std_11)
if(MSVC)
    target_compile_options(test_vmx_cet_vmcs_probe PRIVATE /W4 /WX /GS-)
else()
    target_compile_options(test_vmx_cet_vmcs_probe PRIVATE
        -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_cet_vmcs_probe COMMAND test_vmx_cet_vmcs_probe)

add_test(NAME test_vmx_persistent_msr_read_boundary_contracts
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentMsrReadBoundary.ps1"
        -AbiHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
        -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
        -ExitAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm"
        -PlanHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_read_route_plan.h"
        -PlanSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_read_route_plan.c"
        -DispatchHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.h"
        -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
        -ActivateSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
        -RuntimeSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
        -OwnerSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
        -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
        -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
        -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
        -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
        -GateScript "${CMAKE_SOURCE_DIR}/setup/gate23-msr-read-transparency.ps1"
        -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate24_msr_read_transparency_10cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate24MsrReadTransparency10CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate24-msr-read-transparency-10cycles.ps1"
 -Gate23Path "${CMAKE_SOURCE_DIR}/setup/gate23-msr-read-transparency.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_vmx_persistent_msr_write_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentMsrWriteBoundary.ps1"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -PlanHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_write_route_plan.h"
 -PlanSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_msr_write_route_plan.c"
 -PlanTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_msr_write_route_plan.c"
 -DispatchHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.h"
 -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -DispatchTest "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_persistent_dispatch.c"
 -ControlsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c"
 -VmcsPlanSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c"
 -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate26-msr-transparency.ps1"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate27_msr_bitmap_residency_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate27MsrBitmapResidencyScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate27-msr-bitmap-residency.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate28_msr_native_pass_through_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate28MsrNativePassThroughScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate28-msr-native-pass-through.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate29_msr_native_write_pass_through_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate29MsrNativeWritePassThroughScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate29-msr-native-write-pass-through.ps1"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate30_tsc_native_pass_through_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate30TscNativePassThroughScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate30-tsc-native-pass-through.ps1"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate31_invlpg_native_pass_through_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate31InvlpgNativePassThroughScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate31-invlpg-native-pass-through.ps1"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -ExitAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate32_cet_vmcs_suppression_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate32CetVmcsSuppressionScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate32-cet-vmcs-suppression.ps1"
 -AbiHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -LaunchAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -ExitAsm "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -DriverPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -RuntimeSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -OwnerSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate33_authoritative_status_lifecycle_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate33AuthoritativeStatusLifecycleScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate33-authoritative-status-lifecycle.ps1"
 -LoaderMain "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -LoaderService "${CMAKE_SOURCE_DIR}/src/loader/loader_service.c"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate34_release_security_and_queue_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestReleaseSecurityBoundary.ps1"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -LoaderMain "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate34-status-queue-qualification.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate35_release_candidate_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestReleaseCandidateBoundary.ps1"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate35-release-candidate-lifecycle.ps1"
 -Packager "${CMAKE_SOURCE_DIR}/tools/New-ResearchReleaseCandidate.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -ReadmePath "${CMAKE_SOURCE_DIR}/README.md"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP.md"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

# Runtime V4A: retain the pure VPID/INVVPID capability model under V4 composition.
add_executable(test_vmx_vpid_capability test_vmx_vpid_capability.c)
target_include_directories(test_vmx_vpid_capability PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_vpid_capability PRIVATE hypervisor_core)
target_compile_features(test_vmx_vpid_capability PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_vpid_capability PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_vpid_capability PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_vpid_capability COMMAND test_vmx_vpid_capability)
add_test(NAME test_vmx_vpid_capability_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidCapabilityModel.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_capability.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_capability.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_vpid_capability.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ControlsPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_VNEXT.md"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Runtime V4B: retain pure group-safe ownership under V4 composition.
add_executable(test_vmx_vpid_owner test_vmx_vpid_owner.c)
target_include_directories(test_vmx_vpid_owner PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_vpid_owner PRIVATE hypervisor_core)
target_compile_features(test_vmx_vpid_owner PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_vpid_owner PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_vpid_owner PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_vpid_owner COMMAND test_vmx_vpid_owner)
add_test(NAME test_vmx_vpid_owner_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidOwner.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_owner.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_owner.h"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_vpid_owner.c"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ControlsPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c")

# Runtime V4C: pure INVVPID request/execution plus narrow WDK/MASM adapter.
add_executable(test_vmx_vpid_invalidation test_vmx_vpid_invalidation.c)
target_include_directories(test_vmx_vpid_invalidation PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_vpid_invalidation PRIVATE hypervisor_core)
target_compile_features(test_vmx_vpid_invalidation PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_vpid_invalidation PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_vpid_invalidation PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_vpid_invalidation COMMAND test_vmx_vpid_invalidation)
add_test(NAME test_vmx_vpid_invalidation_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidInvalidation.ps1"
 -ModelSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_invalidation.c"
 -ModelHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_invalidation.h"
 -ExecutorSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_executor.c"
 -ExecutorHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_executor.h"
 -NativeSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_vpid_native_adapter.c"
 -NativeHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_vpid_native_adapter.h"
 -AsmPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_vpid_invvpid.asm"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_vpid_invalidation.c"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -HypervisorCMake "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -TestsCMake "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt"
 -ControlsPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c")

add_executable(test_vmx_vpid_vmcs_coordinator
 test_vmx_vpid_vmcs_coordinator.c)
target_include_directories(test_vmx_vpid_vmcs_coordinator PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_vpid_vmcs_coordinator PRIVATE hypervisor_core)
target_compile_features(test_vmx_vpid_vmcs_coordinator PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_vpid_vmcs_coordinator PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_vpid_vmcs_coordinator PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_vpid_vmcs_coordinator
 COMMAND test_vmx_vpid_vmcs_coordinator)

add_executable(test_vmx_vpid_status_query test_vmx_vpid_status_query.c)
target_include_directories(test_vmx_vpid_status_query PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_vmx_vpid_status_query PRIVATE hypervisor_core)
target_compile_features(test_vmx_vpid_status_query PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_vpid_status_query PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_vpid_status_query PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_vpid_status_query COMMAND test_vmx_vpid_status_query)

add_test(NAME test_vmx_vpid_lifecycle_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidLifecycle.ps1"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/vmx_vpid_status_abi.h"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -OwnerSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_vpid_persistent_owner.c"
 -VmcsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_vmcs_coordinator.c"
 -VmcsAdapterSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_vpid_vmcs_adapter.c"
 -StartSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_start.c"
 -TeardownSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.c"
 -IoctlSource "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -LoaderSource "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate36-vpid-activation.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_VNEXT.md")

add_test(NAME test_gate37_vpid_lifecycle_100cycles_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate37VpidLifecycle100CyclesScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate37-vpid-lifecycle-100cycles.ps1"
 -Gate36Path "${CMAKE_SOURCE_DIR}/setup/gate36-vpid-activation.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_VNEXT.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

add_test(NAME test_gate38_vpid_cr3_residency_interoperability_script_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate38VpidCr3ResidencyInteroperabilityScript.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate38-vpid-cr3-residency-interoperability.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_VNEXT.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

add_test(NAME test_runtime_v4_release_candidate_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestRuntimeV4ReleaseCandidateBoundary.ps1"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate39-runtime-v4-release-candidate-lifecycle.ps1"
 -Packager "${CMAKE_SOURCE_DIR}/tools/New-RuntimeV4ResearchReleaseCandidate.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -ReadmePath "${CMAKE_SOURCE_DIR}/README.md"
 -ReleaseNotesPath "${CMAKE_SOURCE_DIR}/RELEASE_NOTES.md"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_VNEXT.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V5A: authenticated per-processor VM-exit telemetry model and ABI.
add_executable(test_vmx_exit_telemetry test_vmx_exit_telemetry.c)
target_include_directories(test_vmx_exit_telemetry PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_exit_telemetry PRIVATE hypervisor_core)
target_compile_features(test_vmx_exit_telemetry PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_exit_telemetry PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_exit_telemetry PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_exit_telemetry COMMAND test_vmx_exit_telemetry)
add_test(NAME test_vmx_exit_telemetry_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxExitTelemetryBoundary.ps1"
 -TelemetrySource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_exit_telemetry.c"
 -TelemetryHeader "${HYPERSINV_INCLUDE_DIR}/vmx_exit_telemetry.h"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/vmx_telemetry_status_abi.h"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -LaunchAbiPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -LaunchIncPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.inc"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate40-vmexit-telemetry-cpuid-delta.ps1"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_exit_telemetry.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V5.md")

add_test(NAME test_gate41_vmexit_telemetry_route_matrix_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate41VmexitTelemetryRouteMatrix.ps1"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate41-vmexit-telemetry-route-matrix.ps1"
 -LaunchPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -EptRoundtripPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_hook_roundtrip.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V5.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

add_test(NAME test_runtime_v5_release_candidate_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestRuntimeV5ReleaseCandidateBoundary.ps1"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate42-runtime-v5-release-candidate-lifecycle.ps1"
 -Packager "${CMAKE_SOURCE_DIR}/tools/New-RuntimeV5ResearchReleaseCandidate.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -ReadmePath "${CMAKE_SOURCE_DIR}/README.md"
 -ReleaseNotesPath "${CMAKE_SOURCE_DIR}/RELEASE_NOTES.md"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V5.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V6A: authenticated redacted per-processor dispatch diagnostics.
add_executable(test_vmx_dispatch_status test_vmx_dispatch_status.c)
target_include_directories(test_vmx_dispatch_status PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_dispatch_status PRIVATE hypervisor_core)
target_compile_features(test_vmx_dispatch_status PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_dispatch_status PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_dispatch_status PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_dispatch_status COMMAND test_vmx_dispatch_status)
add_test(NAME test_vmx_dispatch_status_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxDispatchStatusBoundary.ps1"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_dispatch_status.c"
 -ModelHeader "${HYPERSINV_INCLUDE_DIR}/vmx_dispatch_status.h"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/vmx_dispatch_status_abi.h"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -LaunchAbiPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate43-dispatch-status-baseline.ps1"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_dispatch_status.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V6.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

add_test(NAME test_gate44_dispatch_status_cpuid_coherence_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate44DispatchCpuidCoherence.ps1"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate44-dispatch-status-cpuid-coherence.ps1"
 -LaunchPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -ProtocolPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_protocol.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V6.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V7A: coherent authenticated telemetry/dispatch observation bundle.
add_executable(test_vmx_observation_status test_vmx_observation_status.c)
target_include_directories(test_vmx_observation_status PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_observation_status PRIVATE hypervisor_core)
target_compile_features(test_vmx_observation_status PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_observation_status PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_observation_status PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_observation_status COMMAND test_vmx_observation_status)
add_test(NAME test_vmx_observation_status_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxObservationStatusBoundary.ps1"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_observation_status.c"
 -ModelHeader "${HYPERSINV_INCLUDE_DIR}/vmx_observation_status.h"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/vmx_observation_status_abi.h"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate45-observation-bundle-cpuid-coherence.ps1"
 -TestPath "${CMAKE_SOURCE_DIR}/src/tests/test_vmx_observation_status.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V7.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V8: one privileged control-device handle at a time.
add_test(NAME test_exclusive_control_device_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestExclusiveControlDeviceBoundary.ps1"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate46-exclusive-control-session.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V8.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V9: require read and write rights for every control session.
add_executable(test_control_session_access
 test_control_session_access.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_access.c")
target_include_directories(test_control_session_access PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_access PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_access PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_access PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_access COMMAND test_control_session_access)
add_test(NAME test_control_session_access_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionAccessBoundary.ps1"
 -AccessModel "${CMAKE_SOURCE_DIR}/src/driver/control_session_access.c"
 -AccessHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_access.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate47-control-session-access-matrix.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V9.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt")

# Runtime V10: accept only the root control-device namespace.
add_executable(test_control_session_namespace
 test_control_session_namespace.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_namespace.c")
target_include_directories(test_control_session_namespace PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_namespace PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_namespace PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_namespace PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_namespace COMMAND test_control_session_namespace)
add_test(NAME test_control_session_namespace_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionNamespaceBoundary.ps1"
 -NamespaceModel "${CMAKE_SOURCE_DIR}/src/driver/control_session_namespace.c"
 -NamespaceHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_namespace.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate48-control-session-namespace.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V10.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt")

# Runtime V11: bind every IOCTL to the process that opened its file object.
add_executable(test_control_session_owner
 test_control_session_owner.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_owner.c")
target_include_directories(test_control_session_owner PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_owner PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_owner PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_owner PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_owner COMMAND test_control_session_owner)
add_test(NAME test_control_session_owner_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionOwnerBoundary.ps1"
 -OwnerModel "${CMAKE_SOURCE_DIR}/src/driver/control_session_owner.c"
 -OwnerHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_owner.h"
 -FileContext "${CMAKE_SOURCE_DIR}/src/driver/control_session_file_context.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ProbeSource "${CMAKE_SOURCE_DIR}/src/loader/control_session_owner_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate49-control-session-owner.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V11.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

add_test(NAME test_control_session_owner_lifetime_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionOwnerLifetimeBoundary.ps1"
 -OwnerModel "${CMAKE_SOURCE_DIR}/src/driver/control_session_owner.c"
 -OwnerHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_owner.h"
 -FileContext "${CMAKE_SOURCE_DIR}/src/driver/control_session_file_context.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ProbeSource "${CMAKE_SOURCE_DIR}/src/loader/control_session_owner_lifetime_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate50-control-session-owner-lifetime.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V12.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

# Runtime V13: exact per-session control-protocol negotiation.
add_executable(test_control_session_protocol
 test_control_session_protocol.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol.c")
target_include_directories(test_control_session_protocol PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_protocol PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_protocol PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_protocol PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_protocol COMMAND test_control_session_protocol)
add_test(NAME test_control_session_protocol_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionProtocolBoundary.ps1"
 -AbiPath "${HYPERSINV_INCLUDE_DIR}/control_session_protocol_abi.h"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol.c"
 -ModelHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol.h"
 -KmdfPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol_kmdf.c"
 -FileContext "${CMAKE_SOURCE_DIR}/src/driver/control_session_file_context.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ClientPath "${CMAKE_SOURCE_DIR}/src/loader/control_session_protocol_client.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -ProbePath "${CMAKE_SOURCE_DIR}/src/loader/control_session_protocol_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate51-control-session-protocol.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V13.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

# Runtime V14: exact input framing for every negotiated operational IOCTL.
add_executable(test_control_session_request_frame
 test_control_session_request_frame.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c")
target_include_directories(test_control_session_request_frame PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_request_frame PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_request_frame PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_request_frame PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_request_frame
 COMMAND test_control_session_request_frame)
add_test(NAME test_control_session_request_frame_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionRequestFrameBoundary.ps1"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ModelHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${HYPERSINV_INCLUDE_DIR}/driver_interface.h"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -ProbePath "${CMAKE_SOURCE_DIR}/src/loader/control_session_request_frame_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate52-control-session-request-framing.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V14.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

# Runtime V15: successful protocol negotiation is immutable per session.
add_test(NAME test_control_session_protocol_replay_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionProtocolReplayBoundary.ps1"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol.c"
 -ModelHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol.h"
 -KmdfPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_protocol_kmdf.c"
 -UnitTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_control_session_protocol.c"
 -ProbePath "${CMAKE_SOURCE_DIR}/src/loader/control_session_protocol_replay_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate53-control-session-protocol-replay.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V15.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

# Runtime V16: exact output framing for fixed-width operational IOCTLs.
add_executable(test_control_session_response_frame
 test_control_session_response_frame.c
 "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c")
target_include_directories(test_control_session_response_frame PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/driver")
target_compile_features(test_control_session_response_frame PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_session_response_frame PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_session_response_frame PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_session_response_frame
 COMMAND test_control_session_response_frame)

add_test(NAME test_control_session_response_frame_boundary_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlSessionResponseFrameBoundary.ps1"
 -ModelPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -ModelHeader "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.h"
 -DriverMain "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -UnitTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_control_session_response_frame.c"
 -ProbePath "${CMAKE_SOURCE_DIR}/src/loader/control_session_response_frame_probe.c"
 -GateScript "${CMAKE_SOURCE_DIR}/setup/gate54-control-session-response-framing.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V16.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -LoaderCMake "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt")

# Runtime V17A: public process-target ABI and pure lifecycle policy.
add_executable(test_vmx_process_target_policy
 test_vmx_process_target_policy.c
 "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_process_target_policy.c")
target_include_directories(test_vmx_process_target_policy PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_compile_features(test_vmx_process_target_policy PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_process_target_policy PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_process_target_policy PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_process_target_policy
 COMMAND test_vmx_process_target_policy)

add_executable(test_vmx_process_target_abi_cpp
 test_vmx_process_target_abi_cpp.cpp)
target_include_directories(test_vmx_process_target_abi_cpp PRIVATE
 "${HYPERSINV_INCLUDE_DIR}")
target_compile_features(test_vmx_process_target_abi_cpp PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(test_vmx_process_target_abi_cpp PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_process_target_abi_cpp PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_process_target_abi_cpp
 COMMAND test_vmx_process_target_abi_cpp)

add_test(NAME test_vmx_process_target_owner_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxProcessTargetOwner.ps1"
 -ProviderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_target_provider.c"
 -ProviderHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_target_provider.h"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.c"
 -OwnerHeader "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.h"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_process_target_policy.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V17.md"
 -DriverCMake "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt")

add_test(NAME test_vmx_process_target_integration_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxProcessTargetIntegration.ps1"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -StartPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_start.c"
 -TeardownPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_ept_persistent_device_teardown.c"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -BoundaryPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_cr3_boundary_roundtrip.c"
 -RouteOwnerPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_owner.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -TestsCMakePath "${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt")

add_test(NAME test_gate55_process_target_lifecycle_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestGate55ProcessTargetLifecycle.ps1"
 -ScriptPath "${CMAKE_SOURCE_DIR}/setup/gate55-process-target-lifecycle.ps1"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.c"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -RouteOwnerPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_cr3_route_owner.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V17.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V18: explicit detached release and process-target rebinding.
add_test(NAME test_vmx_process_target_rebind_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxProcessTargetRebind.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/include/vmx_process_target_abi.h"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_process_target_policy.c"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate56-process-target-rebind.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V18.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V19: generation-matched process-target release.
add_test(NAME test_vmx_process_target_matched_release_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxProcessTargetMatchedRelease.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/include/vmx_process_target_abi.h"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_process_target_policy.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.c"
 -OwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.h"
 -ContextPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_device_context.h"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate57-process-target-matched-release.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V19.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V20: session-owned process-target configuration.
add_test(NAME test_vmx_process_target_configure_self_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxProcessTargetConfigureSelf.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/include/vmx_process_target_abi.h"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_process_target_policy.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.c"
 -OwnerHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_process_target_owner.h"
 -FileContextPath "${CMAKE_SOURCE_DIR}/src/driver/control_session_file_context.h"
 -DriverMainPath "${CMAKE_SOURCE_DIR}/src/driver/driver_main.c"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate58-session-owned-process-target.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V20.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V21: authenticated control capability manifest.
add_executable(test_control_capabilities_policy
 test_control_capabilities_policy.c
 "${CMAKE_SOURCE_DIR}/src/hypervisor/control_capabilities_policy.c")
target_include_directories(test_control_capabilities_policy PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_compile_features(test_control_capabilities_policy PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_control_capabilities_policy PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_control_capabilities_policy PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_control_capabilities_policy
 COMMAND test_control_capabilities_policy)

add_test(NAME test_control_capabilities_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestControlCapabilities.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/include/control_capabilities_abi.h"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/control_capabilities_policy.c"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate59-control-capability-manifest.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V21.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMakePath "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V22: fail-closed VMX substrate admission.
add_executable(test_vmx_platform_compatibility
 test_vmx_platform_compatibility.c
 "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.c")
target_include_directories(test_vmx_platform_compatibility PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_compile_features(test_vmx_platform_compatibility PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_platform_compatibility PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_platform_compatibility PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_platform_compatibility
 COMMAND test_vmx_platform_compatibility)

add_test(NAME test_vmx_platform_compatibility_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPlatformCompatibility.ps1"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.h"
 -AdapterPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_compatibility_adapter.c"
 -AdapterHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_compatibility_adapter.h"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -CoreCMakePath "${CMAKE_SOURCE_DIR}/src/hypervisor/CMakeLists.txt"
 -DriverCMakePath "${CMAKE_SOURCE_DIR}/src/driver/CMakeLists.txt"
 -LoaderCMakePath "${CMAKE_SOURCE_DIR}/src/loader/CMakeLists.txt"
 -TestsCMakePath "${CMAKE_CURRENT_LIST_FILE}"
 -UnitTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_platform_compatibility.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate60-vmx-substrate-admission.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V22.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -CapabilityAbiPath "${CMAKE_SOURCE_DIR}/include/control_capabilities_abi.h")

add_test(NAME test_vmx_platform_consensus_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPlatformConsensus.ps1"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_compatibility.h"
 -AdapterPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_compatibility_adapter.c"
 -AdapterHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_compatibility_adapter.h"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -UnitTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_platform_compatibility.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate61-vmx-substrate-consensus.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V23.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -CapabilityAbiPath "${CMAKE_SOURCE_DIR}/include/control_capabilities_abi.h")

# Runtime V24: topology-wide VMX preentry readiness.
add_executable(test_vmx_platform_readiness test_vmx_platform_readiness.c)
target_include_directories(test_vmx_platform_readiness PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
 "${CMAKE_SOURCE_DIR}/src/hypervisor")
target_link_libraries(test_vmx_platform_readiness PRIVATE hypervisor_core)
target_compile_features(test_vmx_platform_readiness PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_platform_readiness PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_platform_readiness PRIVATE
  -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_platform_readiness COMMAND test_vmx_platform_readiness)

add_test(NAME test_vmx_platform_readiness_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPlatformReadiness.ps1"
 -PolicyPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_readiness.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_readiness.h"
 -AdapterPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_readiness_adapter.c"
 -AdapterHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_readiness_adapter.h"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -ConfigPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_config.c"
 -ConfigHeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_config.h"
 -UnitTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_platform_readiness.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate62-vmx-preentry-readiness.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V24.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -CapabilityAbiPath "${CMAKE_SOURCE_DIR}/include/control_capabilities_abi.h")

# Runtime V25: transactional authenticated HLT VM-exit roundtrip.
add_executable(test_vmx_hlt_probe test_vmx_hlt_probe.c)
target_include_directories(test_vmx_hlt_probe PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/hypervisor"
 "${CMAKE_SOURCE_DIR}/src/driver" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_vmx_hlt_probe PRIVATE hypervisor_core)
target_compile_features(test_vmx_hlt_probe PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_hlt_probe PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_hlt_probe PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_hlt_probe COMMAND test_vmx_hlt_probe)

# Runtime V26: one bounded odd/even admission contract for all diagnostics.
add_executable(test_vmx_diagnostic_state test_vmx_diagnostic_state.c)
target_include_directories(test_vmx_diagnostic_state PRIVATE
 "${HYPERSINV_INCLUDE_DIR}" "${CMAKE_SOURCE_DIR}/src/driver"
 "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(test_vmx_diagnostic_state PRIVATE c_std_11)
if(MSVC)
 target_compile_options(test_vmx_diagnostic_state PRIVATE /W4 /WX /GS-)
else()
 target_compile_options(test_vmx_diagnostic_state PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME test_vmx_diagnostic_state COMMAND test_vmx_diagnostic_state)

add_test(NAME test_vmx_diagnostic_admission_parity_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxDiagnosticAdmissionParity.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -IncPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.inc"
 -ExitPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm"
 -CompositionPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -ActivatePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -UnitPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_diagnostic_state.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V26.md"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate65-diagnostic-admission-matrix.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-DiagnosticAdmissionMatrixPackage.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

add_test(NAME test_vmx_hlt_exit_roundtrip_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxHltExitRoundtrip.ps1"
 -InterfacePath "${CMAKE_SOURCE_DIR}/include/driver_interface.h"
 -CapabilityPath "${CMAKE_SOURCE_DIR}/include/control_capabilities_abi.h"
 -AbiPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -LaunchPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -CompositionPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -ActivatePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
 -RuntimePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_runtime_lifecycle.c"
 -OwnerPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_driver_owner.c"
 -ValidatorPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_validate.c"
 -RequestFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_request_frame.c"
 -ResponseFramePath "${CMAKE_SOURCE_DIR}/src/driver/control_session_response_frame.c"
 -IoctlPath "${CMAKE_SOURCE_DIR}/src/driver/ioctl.c"
 -LoaderPath "${CMAKE_SOURCE_DIR}/src/loader/loader_main.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate64-hlt-exit-roundtrip.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-HltExitRoundtripPackage.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V25.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMakePath "${CMAKE_CURRENT_LIST_FILE}"
 -ExitPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_exit.asm")

# Runtime V27: caller requests cross VM entry only through owned storage.
add_test(NAME test_vmx_diagnostic_payload_ownership_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxDiagnosticPayloadOwnership.ps1"
 -AbiPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch_abi.h"
 -IncPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.inc"
 -LaunchPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_launch.asm"
 -CompositionPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -ActivatePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_activate.c"
 -DispatchPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -StopTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_persistent_stop.c"
 -DispatchTestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_persistent_dispatch.c"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V27.md"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate66-diagnostic-payload-ownership.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-DiagnosticPayloadOwnershipPackage.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V28: hardware-bounded persistent synthetic-guest transactions.
add_test(NAME test_vmx_preemption_timer_watchdog_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPreemptionTimerWatchdog.ps1"
 -ControlsHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.h"
 -ControlsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_controls.c"
 -StateSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_state_ops.c"
 -ReadinessHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_readiness.h"
 -ReadinessSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_platform_readiness.c"
 -ReadinessAdapter "${CMAKE_SOURCE_DIR}/src/driver/vmx_platform_readiness_adapter.c"
 -VmcsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c"
 -CompositionSource "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -DispatchHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.h"
 -DispatchSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_dispatch.c"
 -ControlsTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_controls.c"
 -ReadinessTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_platform_readiness.c"
 -VmcsTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_persistent_vmcs_plan.c"
 -DispatchTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_persistent_dispatch.c"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate67-preemption-watchdog.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-PreemptionWatchdogPackage.ps1"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V28.md"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md"
 -TestsCMake "${CMAKE_CURRENT_LIST_FILE}")

# Runtime V29: attest hardware's current VMCS at every composition boundary.
add_test(NAME test_vmx_current_vmcs_attestation_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxCurrentVmcsAttestation.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.h"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V29.md"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate68-vmcs-binding-attestation.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-VmcsBindingAttestationPackage.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V30: keep current-VMCS attestation on one non-migratable processor.
add_test(NAME test_vmx_execution_context_attestation_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxExecutionContextAttestation.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/driver/vmx_persistent_composition.h"
 -RoadmapPath "${CMAKE_SOURCE_DIR}/ROADMAP_RUNTIME_V30.md"
 -GatePath "${CMAKE_SOURCE_DIR}/setup/gate69-vmcs-execution-context.ps1"
 -PackagerPath "${CMAKE_SOURCE_DIR}/tools/New-VmcsExecutionContextPackage.ps1"
 -DocPath "${CMAKE_SOURCE_DIR}/setup/test.md")

# Runtime V31: multi-field VMCS planners commit only complete unique appends.
add_test(NAME test_vmcs_plan_transaction_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmcsPlanTransactions.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmcs_plan.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmcs_plan.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmcs_plan.c")

# Runtime V32: malformed VMCS plans cannot reach the hardware writer.
add_test(NAME test_vmcs_plan_apply_preflight_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmcsPlanApplyPreflight.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmcs_plan.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmcs_plan.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmcs_plan.c")

# Runtime V33: VMCS-plan publication cannot alias authenticated build inputs.
add_test(NAME test_vmx_persistent_vmcs_plan_span_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentVmcsPlanSpans.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_persistent_vmcs_plan.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_persistent_vmcs_plan.c")

# Runtime V34: every transient VMCS planner must publish its exact batch.
add_test(NAME test_vmx_transient_plan_batch_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxTransientPlanBatches.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_transient.c")

# Runtime V35: transient caller-owned inputs are valid and pairwise disjoint.
add_test(NAME test_vmx_transient_input_span_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxTransientInputSpans.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_transient.c")

# Runtime V36: one validated local transient callback-table snapshot.
add_test(NAME test_vmx_transient_platform_snapshot_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxTransientPlatformSnapshot.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_transient.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_transient.c")

# Runtime V37: disjoint inputs and one frozen launch-probe lifecycle table.
add_test(NAME test_ioctl_launch_probe_lifecycle_snapshot_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestIoctlLaunchProbeLifecycleSnapshot.ps1"
 -SourcePath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_launch_probe.c"
 -HeaderPath "${CMAKE_SOURCE_DIR}/src/hypervisor/ioctl_launch_probe.h"
 -TestPath "${CMAKE_CURRENT_SOURCE_DIR}/test_ioctl.c")

# Runtime V38: disjoint VPID inputs and frozen execution callback tables.
add_test(NAME test_vmx_vpid_frozen_input_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxVpidFrozenInputs.ps1"
 -ExecutorSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_executor.c"
 -ExecutorHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_executor.h"
 -ExecutorTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_vpid_invalidation.c"
 -VmcsSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_vmcs_coordinator.c"
 -VmcsHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_vpid_vmcs_coordinator.h"
 -VmcsTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_vpid_vmcs_coordinator.c")

# Runtime V39: disjoint EPT invalidation inputs and frozen transaction values.
add_test(NAME test_vmx_ept_frozen_invalidation_input_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxEptFrozenInvalidationInputs.ps1"
 -ExecutorSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_executor.c"
 -ExecutorHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_executor.h"
 -ExecutorTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_ept_executor.c"
 -CoordinatorSource "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coordinator.c"
 -CoordinatorHeader "${CMAKE_SOURCE_DIR}/src/hypervisor/vmx_ept_invalidation_coordinator.h"
 -CoordinatorTest "${CMAKE_CURRENT_SOURCE_DIR}/test_vmx_ept_invalidation_coordinator.c")

add_test(NAME test_build_graph_ordering_contracts
 COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
 -File "${CMAKE_SOURCE_DIR}/cmake/TestBuildGraphOrdering.ps1"
 -RootCMakePath "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
 -TestCMakePath "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

if(CMAKE_ASM_MASM_COMPILER)
 add_test(NAME test_vmx_persistent_abi_mirror
  COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
  -File "${CMAKE_SOURCE_DIR}/cmake/TestVmxPersistentAbiMirror.ps1"
  -Ml64Path "${CMAKE_ASM_MASM_COMPILER}")
endif()
