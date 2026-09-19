# Source provenance

The combined repository contains copied working trees. This record preserves
the source identities used to create them without importing their build state
or private credentials.

## UEFILoader

- Source path: `C:\Users\jayli\OneDrive\Documents\Projects\UEFILoader`
- Branch: `feature/insecure-boot-toolkit-p4-m2-loader`
- HEAD: `9917ca60f1957851b7850ce01d784077c4d7f973`
- State: the copied working tree contained local modified and untracked work;
  those files were intentionally included in the component copy.

## Hypervisor

- Source path: `C:\Users\jayli\OneDrive\Documents\Projects\Hypervisor`
- Branch: `main`
- HEAD: `5c21f3681752b37e1a3c7ba4de54339569b76d45`
- State: clean at copy time.

## Explicit exclusions

Build/output directories, generated state, repository history directories, and
the private `hypersinv.pfx` signing key were excluded from the combined source
tree. Test certificates and public provenance files are not signing authority;
the private key must remain outside source control.
