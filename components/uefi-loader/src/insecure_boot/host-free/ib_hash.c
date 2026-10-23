#include "ib_hash.h"
#include "ib_util.h"

static uint32_t ib_hash_rotl32(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32u - count));
}

static uint32_t ib_hash_rotr32(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static uint64_t ib_hash_rotr64(uint64_t value, unsigned count) {
    return (value >> count) | (value << (64u - count));
}

static uint32_t ib_hash_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint64_t ib_hash_read_be64(const uint8_t *bytes) {
    return ((uint64_t)ib_hash_read_be32(bytes) << 32) |
           (uint64_t)ib_hash_read_be32(bytes + 4);
}

static void ib_hash_write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void ib_hash_write_be64(uint8_t *bytes, uint64_t value) {
    ib_hash_write_be32(bytes, (uint32_t)(value >> 32));
    ib_hash_write_be32(bytes + 4, (uint32_t)value);
}

/* ------------------------------------------------------------------------- */
/* SHA-1                                                                     */
/* ------------------------------------------------------------------------- */
static void ib_sha1_compress(ib_sha1_ctx *context, const uint8_t block[64]) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ib_hash_read_be32(block + i * 4u);
    }
    for (i = 16; i < 80; i++) {
        w[i] = ib_hash_rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];

    for (i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        uint32_t temp;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        temp = ib_hash_rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ib_hash_rotl32(b, 30);
        b = a;
        a = temp;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

void ib_sha1_init(ib_sha1_ctx *context) {
    context->state[0] = 0x67452301u;
    context->state[1] = 0xEFCDAB89u;
    context->state[2] = 0x98BADCFEu;
    context->state[3] = 0x10325476u;
    context->state[4] = 0xC3D2E1F0u;
    context->length = 0;
    context->used = 0;
}

static void ib_sha1_block(ib_sha1_ctx *context, const uint8_t *data, size_t length) {
    while (length > 0) {
        size_t room = 64u - context->used;
        size_t take = length < room ? length : room;
        ib_memcpy(context->block + context->used, data, take);
        context->used += take;
        data += take;
        length -= take;
        context->length += (uint64_t)take;
        if (context->used == 64u) {
            ib_sha1_compress(context, context->block);
            context->used = 0;
        }
    }
}

void ib_sha1_update(ib_sha1_ctx *context, const void *data, size_t length) {
    ib_sha1_block(context, (const uint8_t *)data, length);
}

void ib_sha1_final(ib_sha1_ctx *context, uint8_t digest[IB_SHA1_DIGEST_LEN]) {
    uint64_t bits = context->length * 8u;
    unsigned i;
    uint8_t pad = 0x80u;

    ib_sha1_block(context, &pad, 1);
    pad = 0;
    while (context->used != 56u) {
        ib_sha1_block(context, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        context->block[56u + i] = (uint8_t)(bits >> (56u - i * 8u));
    }
    ib_sha1_compress(context, context->block);
    context->used = 0;

    for (i = 0; i < 5; i++) {
        ib_hash_write_be32(digest + i * 4u, context->state[i]);
    }
}

/* ------------------------------------------------------------------------- */
/* SHA-256                                                                   */
/* ------------------------------------------------------------------------- */
static const uint32_t ib_sha256_k[64] = {
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

static void ib_sha256_compress(ib_sha256_ctx *context, const uint8_t block[64]) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ib_hash_read_be32(block + i * 4u);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ib_hash_rotr32(w[i - 15], 7) ^ ib_hash_rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = ib_hash_rotr32(w[i - 2], 17) ^ ib_hash_rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = ib_hash_rotr32(e, 6) ^ ib_hash_rotr32(e, 11) ^ ib_hash_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + ib_sha256_k[i] + w[i];
        uint32_t s0 = ib_hash_rotr32(a, 2) ^ ib_hash_rotr32(a, 13) ^ ib_hash_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void ib_sha256_init(ib_sha256_ctx *context) {
    context->state[0] = 0x6a09e667u;
    context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u;
    context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu;
    context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu;
    context->state[7] = 0x5be0cd19u;
    context->length = 0;
    context->used = 0;
}

static void ib_sha256_block(ib_sha256_ctx *context, const uint8_t *data, size_t length) {
    while (length > 0) {
        size_t room = 64u - context->used;
        size_t take = length < room ? length : room;
        ib_memcpy(context->block + context->used, data, take);
        context->used += take;
        data += take;
        length -= take;
        context->length += (uint64_t)take;
        if (context->used == 64u) {
            ib_sha256_compress(context, context->block);
            context->used = 0;
        }
    }
}

void ib_sha256_update(ib_sha256_ctx *context, const void *data, size_t length) {
    ib_sha256_block(context, (const uint8_t *)data, length);
}

void ib_sha256_final(ib_sha256_ctx *context, uint8_t digest[IB_SHA256_DIGEST_LEN]) {
    uint64_t bits = context->length * 8u;
    unsigned i;
    uint8_t pad = 0x80u;

    ib_sha256_block(context, &pad, 1);
    pad = 0;
    while (context->used != 56u) {
        ib_sha256_block(context, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        context->block[56u + i] = (uint8_t)(bits >> (56u - i * 8u));
    }
    ib_sha256_compress(context, context->block);
    context->used = 0;

    for (i = 0; i < 8; i++) {
        ib_hash_write_be32(digest + i * 4u, context->state[i]);
    }
}

/* ------------------------------------------------------------------------- */
/* SHA-512 / SHA-384                                                         */
/* ------------------------------------------------------------------------- */
static const uint64_t ib_sha512_k[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full,
    0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull,
    0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
    0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull,
    0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full,
    0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull,
    0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull,
    0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
    0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull,
    0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull,
    0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull,
    0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull,
    0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
    0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull
};

static void ib_sha512_compress(ib_sha512_ctx *context, const uint8_t block[128]) {
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ib_hash_read_be64(block + i * 8u);
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ib_hash_rotr64(w[i - 15], 1) ^ ib_hash_rotr64(w[i - 15], 8) ^
                      (w[i - 15] >> 7);
        uint64_t s1 = ib_hash_rotr64(w[i - 2], 19) ^ ib_hash_rotr64(w[i - 2], 61) ^
                      (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (i = 0; i < 80; i++) {
        uint64_t s1 = ib_hash_rotr64(e, 14) ^ ib_hash_rotr64(e, 18) ^ ib_hash_rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t temp1 = h + s1 + ch + ib_sha512_k[i] + w[i];
        uint64_t s0 = ib_hash_rotr64(a, 28) ^ ib_hash_rotr64(a, 34) ^ ib_hash_rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void ib_sha512_begin(ib_sha512_ctx *context, const uint64_t iv[8]) {
    unsigned i;
    for (i = 0; i < 8; i++) {
        context->state[i] = iv[i];
    }
    context->length_low = 0;
    context->length_high = 0;
    context->used = 0;
}

void ib_sha512_init(ib_sha512_ctx *context) {
    static const uint64_t iv[8] = {
        0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
        0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
        0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
        0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull
    };
    ib_sha512_begin(context, iv);
}

void ib_sha384_init(ib_sha512_ctx *context) {
    static const uint64_t iv[8] = {
        0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull,
        0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
        0x67332667ffc00b31ull, 0x8eb44a8768581511ull,
        0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull
    };
    ib_sha512_begin(context, iv);
}

static void ib_sha512_block(ib_sha512_ctx *context, const uint8_t *data, size_t length) {
    while (length > 0) {
        size_t room = 128u - context->used;
        size_t take = length < room ? length : room;
        ib_memcpy(context->block + context->used, data, take);
        context->used += take;
        data += take;
        length -= take;
        context->length_low += (uint64_t)take;
        if (context->length_low < (uint64_t)take) {
            context->length_high++;
        }
        if (context->used == 128u) {
            ib_sha512_compress(context, context->block);
            context->used = 0;
        }
    }
}

void ib_sha512_update(ib_sha512_ctx *context, const void *data, size_t length) {
    ib_sha512_block(context, (const uint8_t *)data, length);
}

void ib_sha384_update(ib_sha512_ctx *context, const void *data, size_t length) {
    ib_sha512_block(context, (const uint8_t *)data, length);
}

static void ib_sha512_finish(ib_sha512_ctx *context) {
    uint64_t bits_low = context->length_low << 3;
    uint64_t bits_high = (context->length_high << 3) | (context->length_low >> 61);
    unsigned i;
    uint8_t pad = 0x80u;

    ib_sha512_block(context, &pad, 1);
    pad = 0;
    while (context->used != 112u) {
        ib_sha512_block(context, &pad, 1);
    }
    for (i = 0; i < 8; i++) {
        context->block[112u + i] = (uint8_t)(bits_high >> (56u - i * 8u));
        context->block[120u + i] = (uint8_t)(bits_low >> (56u - i * 8u));
    }
    ib_sha512_compress(context, context->block);
    context->used = 0;
}

void ib_sha512_final(ib_sha512_ctx *context, uint8_t digest[IB_SHA512_DIGEST_LEN]) {
    unsigned i;
    ib_sha512_finish(context);
    for (i = 0; i < 8; i++) {
        ib_hash_write_be64(digest + i * 8u, context->state[i]);
    }
}

void ib_sha384_final(ib_sha512_ctx *context, uint8_t digest[IB_SHA384_DIGEST_LEN]) {
    unsigned i;
    ib_sha512_finish(context);
    for (i = 0; i < 6; i++) {
        ib_hash_write_be64(digest + i * 8u, context->state[i]);
    }
}
