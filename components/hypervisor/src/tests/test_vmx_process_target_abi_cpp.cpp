#include "vmx_process_target_abi.h"

int main()
{
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response{};
    HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST release{};
    HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST matched{};
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST configure_self{};
    return response.Version == 0 && release.Version == 0 &&
        matched.Version == 0 && configure_self.Version == 0 ? 0 : 1;
}
