/*
 * sha256.c - Freestanding SHA-256 one-shot hashing.
 *
 * FIPS 180-4, section 6.2.  This file deliberately avoids libc, compiler
 * intrinsics, dynamic allocation, and UEFI types so it is usable by both the
 * host test harness and the EFI loader.
 */
#include "sha256.h"

static const uint32_t g_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr32(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32u - amount));
}

static uint32_t load_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void store_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha256_compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t schedule[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        schedule[i] = load_be32(block + i * 4u);
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr32(schedule[i - 15u], 7u) ^
                      rotr32(schedule[i - 15u], 18u) ^
                      (schedule[i - 15u] >> 3);
        uint32_t s1 = rotr32(schedule[i - 2u], 17u) ^
                      rotr32(schedule[i - 2u], 19u) ^
                      (schedule[i - 2u] >> 10);
        schedule[i] = schedule[i - 16u] + s0 + schedule[i - 7u] + s1;
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (i = 0; i < 64u; i++) {
        uint32_t s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choose + g_sha256_k[i] + schedule[i];
        uint32_t s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

sha256_status_t sha256_hash(const uint8_t *data,
                            size_t length,
                            uint8_t digest[SHA256_DIGEST_SIZE])
{
    static const uint32_t initial_state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint32_t state[8];
    uint8_t final_blocks[128];
    size_t full_length;
    size_t remainder;
    size_t final_length;
    uint64_t bit_length;
    uint32_t i;

    if (digest == NULL || (data == NULL && length != 0u)) {
        return SHA256_ERR_ARGUMENT;
    }
    /* SHA-256 encodes the message length in an unsigned 64-bit field. */
    if (length > (size_t)(UINT64_MAX / 8u)) {
        return SHA256_ERR_ARGUMENT;
    }

    for (i = 0; i < 8u; i++) {
        state[i] = initial_state[i];
    }

    full_length = length - (length % 64u);
    /* Use size_t for the block offset: an input may contain more than 2^32
     * blocks on a 64-bit host, and truncating the block count would silently
     * produce an incorrect digest. */
    {
        size_t block_offset = 0u;
        while (block_offset < full_length) {
            sha256_compress(state, data + block_offset);
            block_offset += 64u;
        }
    }

    remainder = length % 64u;
    final_length = (remainder < 56u) ? 64u : 128u;
    for (i = 0; i < 128u; i++) {
        final_blocks[i] = 0u;
    }
    for (i = 0; i < (uint32_t)remainder; i++) {
        final_blocks[i] = data[full_length + i];
    }
    final_blocks[remainder] = 0x80u;

    bit_length = (uint64_t)length * 8u;
    for (i = 0; i < 8u; i++) {
        final_blocks[final_length - 1u - i] =
            (uint8_t)(bit_length >> (i * 8u));
    }
    sha256_compress(state, final_blocks);
    if (final_length == 128u) {
        sha256_compress(state, final_blocks + 64u);
    }

    for (i = 0; i < 8u; i++) {
        store_be32(digest + i * 4u, state[i]);
    }
    return SHA256_OK;
}
