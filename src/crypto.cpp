#include "crypto.h"
#include "mbedtls/aes.h"
#include <cstring>

bool aes256_cbc_decrypt(const uint8_t *key, const uint8_t *input, size_t input_len,
                        uint8_t *output, size_t *output_len)
{
    if (!key || !input || !output || !output_len)
        return false;

    // Need at least IV (16 bytes) + one block of ciphertext (16 bytes)
    if (input_len < AES_IV_SIZE + AES_BLOCK_SIZE)
        return false;

    size_t ciphertext_len = input_len - AES_IV_SIZE;

    // Ciphertext must be a multiple of block size
    if (ciphertext_len % AES_BLOCK_SIZE != 0)
        return false;

    // Copy IV since mbedtls_aes_crypt_cbc modifies it in-place
    uint8_t iv[AES_IV_SIZE];
    memcpy(iv, input, AES_IV_SIZE);

    const uint8_t *ciphertext = input + AES_IV_SIZE;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int ret = mbedtls_aes_setkey_dec(&ctx, key, 256);
    if (ret != 0)
    {
        mbedtls_aes_free(&ctx);
        return false;
    }

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, ciphertext_len, iv, ciphertext, output);
    mbedtls_aes_free(&ctx);

    if (ret != 0)
        return false;

    // PKCS7 unpadding
    uint8_t pad_value = output[ciphertext_len - 1];
    if (pad_value == 0 || pad_value > AES_BLOCK_SIZE)
        return false;

    // Verify all padding bytes
    for (uint8_t i = 0; i < pad_value; i++)
    {
        if (output[ciphertext_len - 1 - i] != pad_value)
            return false;
    }

    *output_len = ciphertext_len - pad_value;
    return true;
}

static uint8_t hex_char_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0xFF;
}

bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    if (!hex || !out)
        return false;

    for (size_t i = 0; i < out_len; i++)
    {
        uint8_t hi = hex_char_to_nibble(hex[i * 2]);
        uint8_t lo = hex_char_to_nibble(hex[i * 2 + 1]);
        if (hi == 0xFF || lo == 0xFF)
            return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}
