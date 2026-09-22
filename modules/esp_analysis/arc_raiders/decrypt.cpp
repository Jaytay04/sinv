#ifdef MODULE_ESP

#include <cstdint>
#include <cstring>

namespace hypersinv::esp::arc_raiders {

/*
 * TODO(2026-09-22 patch): routines below are stale CL-1372005. The new
 * FField/FName/FString decrypt chains are specified in
 * arc_raiders/specs/patch_2026_09_22_offsets.json; rewrite once
 * live-verified in the VM.
 */

/*
 * Rolling XOR decryption for FName indices.
 * 
 * For byte i:
 *   result[i] = input[i] ^ (key[i % key_len] ^ low8(state))
 *   state = rol32(state + step + keyByte, rotate)
 * 
 * This is symmetric: applying it twice restores the original.
 */
static uint32_t rol32(uint32_t value, int shift) {
    if ((shift &= 31) == 0) return value;
    return (value << shift) | (value >> (32 - shift));
}

void DecryptFName(const uint8_t* input, uint8_t* output, size_t length,
                  const uint8_t* key, size_t key_len,
                  uint32_t seed, uint32_t step, int rotate) {
    uint32_t state = seed;
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t key_byte = key[i % key_len];
        uint8_t xor_value = key_byte ^ (state & 0xFF);
        output[i] = input[i] ^ xor_value;
        
        state = rol32(state + step + key_byte, rotate);
    }
}

/*
 * Affine substitution decryption for strings.
 * 
 * For each byte: result = (byte * multiplier + addend) mod 256
 * Multiplier must be odd for the transformation to be invertible.
 */
void DecryptString(const uint8_t* input, char* output, size_t length,
                   uint8_t multiplier, uint8_t addend) {
    for (size_t i = 0; i < length; ++i) {
        output[i] = static_cast<char>((input[i] * multiplier + addend) & 0xFF);
        if (output[i] == '\0') break;
    }
    output[length - 1] = '\0';
}

/*
 * Inverse affine substitution for decryption.
 * 
 * Computes the modular multiplicative inverse of multiplier mod 256,
 * then applies: result = ((byte - addend) * inverse_multiplier) mod 256
 */
void DecryptStringInverse(const char* input, uint8_t* output, size_t length,
                          uint8_t multiplier, uint8_t addend) {
    /* Find modular inverse of multiplier mod 256 */
    uint8_t inverse = 1;
    for (uint8_t i = 1; i < 256; i += 2) {
        if ((multiplier * i) % 256 == 1) {
            inverse = i;
            break;
        }
    }
    
    for (size_t i = 0; i < length; ++i) {
        int val = (static_cast<uint8_t>(input[i]) - addend + 256) % 256;
        output[i] = static_cast<uint8_t>((val * inverse) % 256);
        if (output[i] == 0) break;
    }
}

} // namespace hypersinv::esp::arc_raiders

#endif /* MODULE_ESP */

