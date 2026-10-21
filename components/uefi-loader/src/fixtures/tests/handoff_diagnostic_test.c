#include <stdio.h>
#include <string.h>
#include "handoff_diagnostic.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    UINT8 buffer[HANDOFF_DIAGNOSTIC_MAX_SIZE];
    UINT8 snapshot[HANDOFF_DIAGNOSTIC_MAX_SIZE];
    const char *phases[] = { "read", "device_path", "load", "start", "return" };
    const char *targets[] = { "inventory", "boot_manager" };
    const char expected[] = "schema=1\nhandoff_boot_manager_start_end_status=0x8000000000000007\n";
    UINTN used = 9u, prior, target, phase;
    memset(buffer, 0xA5, sizeof(buffer));
    memcpy(buffer, "schema=1\n", used);
    CHECK(handoff_diagnostic_append(buffer, sizeof(buffer), &used, "boot_manager", "start", TRUE, EFI_DEVICE_ERROR, NULL, 0));
    CHECK(used == sizeof(expected) - 1u && memcmp(buffer, expected, used) == 0);
    CHECK(buffer[used] == 0xA5);
    memcpy(snapshot, buffer, sizeof(buffer)); prior = used;
    CHECK(!handoff_diagnostic_append(buffer, used + 1u, &used, "boot_manager", "start", FALSE, EFI_NOT_STARTED, NULL, 0));
    CHECK(used == prior && memcmp(buffer, snapshot, sizeof(buffer)) == 0);
    CHECK(!handoff_diagnostic_append(buffer, sizeof(buffer), &used, "unknown", "start", TRUE, 0, NULL, 0));
    CHECK(!handoff_diagnostic_append(buffer, sizeof(buffer), &used, "inventory", "unknown", TRUE, 0, NULL, 0));
    CHECK(!handoff_diagnostic_append(buffer, sizeof(buffer), &used, "inventory", "load", TRUE, 0, "bad\nkey", 0));
    CHECK(used == prior && memcmp(buffer, snapshot, sizeof(buffer)) == 0);
    used = 9u;
    for (target = 0; target < 2u; target++) {
        for (phase = 0; phase < 5u; phase++) {
            CHECK(handoff_diagnostic_append(buffer, sizeof(buffer), &used, targets[target], phases[phase], FALSE, EFI_NOT_STARTED, NULL, 0));
            CHECK(handoff_diagnostic_append(buffer, sizeof(buffer), &used, targets[target], phases[phase], TRUE, EFI_SUCCESS,
                phase == 2u ? "firmware_load" : NULL, EFI_SUCCESS));
        }
    }
    CHECK(used < HANDOFF_DIAGNOSTIC_MAX_SIZE);
    buffer[used] = 0;
    CHECK(strstr((const char *)buffer, "handoff_boot_manager_load_end_status=0x0000000000000000\n") != NULL);
    CHECK(strstr((const char *)buffer, "handoff_boot_manager_shim_phase=firmware_load\n") != NULL);
    CHECK(strstr((const char *)buffer, "handoff_boot_manager_shim_status=0x0000000000000000\n") != NULL);
    puts("handoff diagnostic tests passed");
    return 0;
}
