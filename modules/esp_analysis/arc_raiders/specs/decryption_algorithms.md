# Data Transformation Specifications

## FName Decryption
- Algorithm: Rolling XOR with FNV-1a 64-bit
- Prime: 0x1000193
- Rotation amounts: [13, 22, 19] per round
- Subtractive constant: 0x400062AA (CL-1372005)

## Player Name Decryption
- Algorithm: Per-character XOR with rotating state
- State update: state = (state + rol32(state, ROL)) * PRIME
- Character decrypt: c = c ^ (state & 0x1F)
- Coset rotation: [+47, +5, +13, +13] for ASCII ranges
- Constants: PRIME=0x1000193, ADD=0x400062AA, ROL=21

## UObject Hash Verification
- Three-round IMUL/ROL for slot indexing
- See pointer_chains.cpp for implementation