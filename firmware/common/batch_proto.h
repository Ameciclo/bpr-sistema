#pragma once
/*
 * BPR — Contrato de bytes do protocolo de lotes (PRD transferência de
 * lotes, integridade e recibos — passo 1 do plano de implementação).
 *
 * Header-only inline: o diretório comum firmware/common entra nos builds
 * por include-path (sem fonte compilável separado); as duas pontas
 * (firmware C++ e backend TS) implementam o MESMO contrato e concordam por
 * vetores compartilhados (T17) — ver docs/BATCH_PROTOCOL.md.
 *
 * Representação canônica do hash do lote (todos os inteiros LITTLE-ENDIAN):
 *
 *   "BTP1"                 4 bytes   (versão do formato de lote)
 *   bike_id               10 bytes   (ASCII "bpr-XXXXXX", SEM terminador)
 *   session_id             u32 LE    (identidade do lote — parte 1)
 *   session_salt           u16 LE    (identidade do lote — parte 2)
 *   total_parts            u16 LE
 *   total_bytes            u32 LE    (soma dos comprimentos das partes)
 *   para cada parte i em 0..total_parts-1, EM ORDEM DE ÍNDICE:
 *     part_index           u16 LE
 *     part_len             u16 LE
 *     part_bytes           part_len bytes (conteúdo exato da parte)
 *
 *   SHA-256 (FIPS 180-4) sobre esses bytes = batch_sha256 (32 bytes).
 *
 * CRC por parte: CRC-32/ISO-HDLC (refletido, poly 0xEDB88320, init
 * 0xFFFFFFFF, xorout 0xFFFFFFFF) sobre os bytes da parte — a MESMA variante
 * usada nos demais checksums do sistema, aqui com implementação única.
 *
 * Convenção de índices: 0..total-1 (declarada nesta versão do protocolo;
 * os exemplos humanos do PRD usavam 1..N — a implementação é 0-based e
 * valida os limites sem ambiguidade).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define BATCH_PROTO_VERSION 1
#define BATCH_PROTO_MAGIC "BTP1"
#define BATCH_PROTO_BIKE_ID_LEN 10   /* "bpr-XXXXXX" ASCII (4+6) */
#define BATCH_PROTO_MAX_PARTS 512
#define BATCH_PROTO_MAX_PART_BYTES 460 /* orçamento de aplicação vigente */

/* Estado do lote na central (PRD §5.1) */
typedef enum {
    BATCH_STATE_RECEIVING = 1,       /* algumas partes persistidas */
    BATCH_STATE_COMPLETE_LOCAL = 2,  /* partes + hash global conferem */
    BATCH_STATE_AWAITING_CLOUD = 3,  /* completo local, sem confirmação remota */
    BATCH_STATE_HASH_MISMATCH = 4    /* checksums ok, hash global divergiu */
} batch_state_t;

/* ------------------------------------------------------------------ */
/* CRC-32/ISO-HDLC canônico (inline)                                   */
/* ------------------------------------------------------------------ */
inline uint32_t batch_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-4) — streaming, portátil (inline)                 */
/* ------------------------------------------------------------------ */
struct batch_sha256_ctx {
    uint32_t h[8];
    uint64_t total_len;
    uint8_t block[64];
    size_t block_len;

    void init() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85;
        h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c;
        h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        total_len = 0;
        block_len = 0;
    }

    void update(const uint8_t *data, size_t len) {
        total_len += len;
        while (len > 0) {
            size_t take = 64 - block_len;
            if (take > len) take = len;
            memcpy(block + block_len, data, take);
            block_len += take;
            data += take;
            len -= take;
            if (block_len == 64) {
                compress(block);
                block_len = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        uint64_t bits = total_len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (block_len != 56) {
            update(&zero, 1);
        }
        uint8_t lenb[8];
        for (int i = 0; i < 8; i++) {
            lenb[i] = (uint8_t)(bits >> (56 - i * 8));
        }
        update(lenb, 8);

        for (int i = 0; i < 8; i++) {
            out[i * 4] = (uint8_t)(h[i] >> 24);
            out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
            out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
            out[i * 4 + 3] = (uint8_t)(h[i]);
        }
    }

private:
    static uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void compress(const uint8_t block[64]);
};

inline void batch_sha256_hex(const uint8_t hash[32], char out[65]) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = hexd[hash[i] >> 4];
        out[i * 2 + 1] = hexd[hash[i] & 0xF];
    }
    out[64] = '\0';
}

/* ------------------------------------------------------------------ */
/* Hash input canônico do lote (streaming, inline)                     */
/* ------------------------------------------------------------------ */
struct batch_hash_input {
    batch_sha256_ctx sha;

    void begin(const char bike_id[BATCH_PROTO_BIKE_ID_LEN],
               uint32_t session_id, uint16_t session_salt,
               uint16_t total_parts, uint32_t total_bytes) {
        sha.init();
        sha.update((const uint8_t *)BATCH_PROTO_MAGIC, 4);
        sha.update((const uint8_t *)bike_id, BATCH_PROTO_BIKE_ID_LEN);
        put_u32(session_id);
        put_u16(session_salt);
        put_u16(total_parts);
        put_u32(total_bytes);
    }

    void part(uint16_t part_index, const uint8_t *part_bytes, uint16_t part_len) {
        put_u16(part_index);
        put_u16(part_len);
        sha.update(part_bytes, part_len);
    }

    void final(uint8_t out[32]) {
        sha.final(out);
    }

private:
    void put_u16(uint16_t v) {
        uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
        sha.update(b, 2);
    }
    void put_u32(uint32_t v) {
        uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
        sha.update(b, 4);
    }
};

/* ------------------------------------------------------------------ */
/* Auto-verificação — vetores FIPS 180-4 + CRC clássico                 */
/* ------------------------------------------------------------------ */
inline int batch_proto_selftest(void) {
    struct {
        const char *msg;
        const char *hex;
    } vectors[] = {
        {"abc",
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        batch_sha256_ctx ctx;
        ctx.init();
        ctx.update((const uint8_t *)vectors[v].msg, strlen(vectors[v].msg));
        uint8_t out[32];
        ctx.final(out);
        char hex[65];
        batch_sha256_hex(out, hex);
        if (strcmp(hex, vectors[v].hex) != 0) return 0;
    }
    if (batch_crc32((const uint8_t *)"123456789", 9) != 0xCBF43926u) return 0;
    return 1;
}

/* Definição inline do round function (usada pelos métodos acima) */
inline void batch_sha256_ctx::compress(const uint8_t block[64]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
