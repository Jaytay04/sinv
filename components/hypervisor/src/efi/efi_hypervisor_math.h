#ifndef HYPERSINV_EFI_HYPERVISOR_MATH_H
#define HYPERSINV_EFI_HYPERVISOR_MATH_H

/*
 * Pure control-policy helpers for the 1:1 host-OS VMX profile.
 *
 * Intel defines the pin-based VM-execution controls as follows:
 *   bit 0  external-interrupt exiting
 *   bit 3  NMI exiting
 *   bit 5  virtual NMIs
 *   bit 7  process posted interrupts
 *
 * The resident VM-exit path has no APIC/device emulation or posted-interrupt
 * descriptor.  These controls must therefore be absent from the final
 * selected value, including bits forced by the capability MSR's allowed-0
 * half.  The helpers below do not access VMX hardware.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_VM_PIN_EXTERNAL_INTERRUPT_EXITING (UINT32_C(1) << 0)
#define HV_EFI_VM_PIN_NMI_EXITING (UINT32_C(1) << 3)
#define HV_EFI_VM_PIN_VIRTUAL_NMIS (UINT32_C(1) << 5)
#define HV_EFI_VM_PIN_PROCESS_POSTED_INTERRUPTS (UINT32_C(1) << 7)
#define HV_EFI_VM_PIN_UNSUPPORTED_DIRECT_INTERRUPT_MASK \
    (HV_EFI_VM_PIN_EXTERNAL_INTERRUPT_EXITING | \
     HV_EFI_VM_PIN_NMI_EXITING | \
     HV_EFI_VM_PIN_VIRTUAL_NMIS | \
     HV_EFI_VM_PIN_PROCESS_POSTED_INTERRUPTS)

/* Validates a final, already-adjusted pin-based control value. */
int HvEfiDirectInterruptPinControlsValid(uint32_t selectedPinControls);

/*
 * Applies Intel's (desired | allowed-0) & allowed-1 rule and rejects any
 * forbidden direct-delivery bit in the requested or forced result.  The
 * capability argument is an IA32_VMX_{TRUE,}PINBASED_CTLS value.
 */
int HvEfiSelectDirectInterruptPinControls(
    uint32_t desiredPinControls,
    uint64_t pinCapabilities,
    uint32_t *selectedPinControls);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_HYPERVISOR_MATH_H */
