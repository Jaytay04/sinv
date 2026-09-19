# Runtime V46 - Canonical EPT Hook Allocator Admission

## Problem

Runtime V45 required the hook-owner allocator input to be disjoint from the
session owner. The production call intentionally passes the authenticated
allocator embedded in that session (`&session->Allocator`), so cumulative
Gate 69 rejected hook initialization before the violation roundtrip.

## Implementation

- Admit exactly `&session->Allocator` as the canonical embedded allocator.
- Validate the session before reading that embedded allocator.
- Snapshot the admitted allocator and retain the frozen allocation, cleanup,
  and destruction contracts added in Runtime V45.
- Continue rejecting every other allocator/session overlap and every overlap
  with the hook-owner output or session-owned storage.
- Cover the production embedded-allocator route in the hook-owner unit and
  source-ordering contracts.

## Static acceptance

Fresh WDK/MSVC/MASM build directory:
`build/codex-runtime-v46-20260907-034254`

- Complete build graph: `457/457`
- Focused hook-owner tests: `2/2`
- Complete CTest suite: `302/302`
- Driver, loader, VMX launch object, and VMX capture object present

## Live acceptance

Cumulative Gate 69 passed from the immutable signed Runtime V46 package:

- Source commit: `f452bc4d669a69efdba8db84387e52d78ffa5bae`
- Package SHA-256:
  `B25DB25925FABE57776EBDA6F175E5E1AF4437C669D83CDD429A4E9F7BC5F776`
- RunId: `gate69-vmcs-execution-context-20260907-034955-820aa8193b`
- Evidence archive SHA-256:
  `6AAEEA301A7280AF2229B3C632021A8E43DAF1640CCDEB29B3D9656A1BC8B23A`
- Closed KD log SHA-256:
  `EC9F2A49BE0FE035E85D89B1824743111EC8987496E6AEA67B30DED0A2EC4F2D`

The guest completed all seven diagnostics, authoritative status, canonical
unload, and service removal with empty stderr. KD recorded four launch and
eleven resume VMCS-binding attestations with no fatal, mismatch, bugcheck, or
hook-add failure marker. The broker restored and powered off the configured VM
and released its resource lease.

## Manual operator repeat

The user repeated the unchanged Gate 69 package manually on 2026-09-08.
Evidence archive SHA-256 is
`C2039CE70AC96AF8B5E293DFF616888E4725DB71AFE832E5BB87ACC650269527`;
closed KD log SHA-256 is
`5F445900A96AE501555815BD4FB1A841E830E801123F24D57265E16D2FA94B3F`.
The run again completed all seven diagnostics, authoritative status, canonical
unload, and service removal with empty stderr and no failure events. KD again
recorded four launch and eleven resume attestations with no fatal, mismatch,
failing status, or bugcheck marker. This confirms reproducibility without
creating a new gate identity.
