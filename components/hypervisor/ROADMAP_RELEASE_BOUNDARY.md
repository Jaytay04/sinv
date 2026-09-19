# Production Boundary Release Closure

Status: complete; Gate 63 accepted.

## Problem

The accepted persistent runtime coexisted with an older VM-exit simulation and
unsupported hardware-I/O wrappers in the WDK-free `hypervisor_core` archive.
Those symbols had no production callers, but their false-success fallbacks and
public declarations made the production boundary ambiguous.

## Decision

Keep the historical VM-exit model only as source for its dedicated user-mode
test. Remove unsupported CPU/MTRR I/O wrappers and legacy VPID/dispatch names
from the production archive and public API. Hardware sampling remains owned by
explicit WDK adapters; live dispatch remains owned by the persistent runtime.

## Acceptance

1. `hypervisor_core.lib` contains none of the quarantined legacy symbols.
2. Driver sources cannot reference those symbols.
3. The legacy VM-exit model remains testable but is compiled only by
   `test_vmexit`.
4. Mutation-backed static contracts reject reinsertion into the core, public
   API, or driver.
5. A fresh WDK/MSVC/MASM build and complete CTest pass.
6. Gate 63 binds the clean pushed commit and signed artifacts to one canonical
   LOAD/status/UNLOAD lifecycle on the disposable two-group/four-processor VM.

## Live-test scope

Gate 63 is necessary because the driver binary changes even though supported
runtime behavior does not. One lifecycle is proportionate; repetition gates
are not. The closed KD log must retain the accepted substrate-consensus and
VMX-preentry-consensus markers. Any failure is preserved without retry.

## Accepted evidence

- Source commit: `64bbdea0d2058d6f23e708846e05b61ed722b620`.
- Package SHA-256:
  `BACEC94E6E5846DDD78F2DED2F7134742E912B42AED8A3E8196D69655DC17373`.
- RunId:
  `gate63-production-boundary-release-candidate-20260906-002136-52475adada`.
- Evidence archive SHA-256:
  `09576B03D3CAEE6F40E7641FDBA3B7ED97B76CC870E42DEEE5930AB1831E37DE`.
- Closed KD log SHA-256:
  `7E9EBAFB09F533CE79E502D150EB5C27E51D215D7A400703DEBEF35A4ADFF651`.

Gate 63 completed one canonical LOAD/status/UNLOAD lifecycle on all four
processors across two groups. It preserved exact capability manifest `1` /
`0x00000000000003FF` / `29`, persistent/EPT/combined readiness, inactive CR3
observation, service absence, empty stderr, and no failure events. The closed
KD log contains both accepted topology-wide substrate and preentry consensus
markers. The broker powered off the VM and restored the clean snapshot.

The first publication was rejected before guest execution because it omitted
the host-runner `SHA256SUMS.txt` envelope. Commit `64bbdea` added deterministic
package checksums and mutation coverage; the rejected package and RunId were
not reused.
