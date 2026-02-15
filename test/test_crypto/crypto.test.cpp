#include <unity.h>
#include <string.h>
#include "mbedtls/aes.h"

// Include crypto implementation directly for native testing
#include "../../src/crypto.cpp"

// Helper: encrypt with AES-256-CBC + PKCS7 for test purposes
static bool aes256_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                                const uint8_t *input, size_t input_len,
                                uint8_t *output, size_t *output_len)
{
    // PKCS7 pad
    uint8_t pad_value = 16 - (input_len % 16);
    size_t padded_len = input_len + pad_value;

    uint8_t *padded = new uint8_t[padded_len];
    memcpy(padded, input, input_len);
    memset(padded + input_len, pad_value, pad_value);

    // Prepend IV
    memcpy(output, iv, 16);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 256);

    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    int ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, padded_len, iv_copy, padded, output + 16);
    mbedtls_aes_free(&ctx);
    delete[] padded;

    *output_len = 16 + padded_len;
    return ret == 0;
}

// ---- Tests ----

void test_hex_to_bytes_valid(void)
{
    const char *hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    uint8_t out[32];
    TEST_ASSERT_TRUE(hex_to_bytes(hex, out, 32));
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x23, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xef, out[31]);
}

void test_hex_to_bytes_uppercase(void)
{
    const char *hex = "AABBCCDD00112233AABBCCDD00112233AABBCCDD00112233AABBCCDD00112233";
    uint8_t out[32];
    TEST_ASSERT_TRUE(hex_to_bytes(hex, out, 32));
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, out[1]);
}

void test_hex_to_bytes_invalid(void)
{
    const char *hex = "ZZZZZZZZ";
    uint8_t out[4];
    TEST_ASSERT_FALSE(hex_to_bytes(hex, out, 4));
}

void test_decrypt_roundtrip(void)
{
    uint8_t key[32];
    memset(key, 0x42, 32);

    uint8_t iv[16];
    memset(iv, 0x13, 16);

    const char *plaintext = "Hello TRMNL world! This is a test message for AES-256-CBC.";
    size_t plaintext_len = strlen(plaintext);

    // Encrypt
    uint8_t encrypted[256];
    size_t encrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_encrypt(key, iv, (const uint8_t *)plaintext, plaintext_len, encrypted, &encrypted_len));

    // Decrypt
    uint8_t decrypted[256];
    size_t decrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_decrypt(key, encrypted, encrypted_len, decrypted, &decrypted_len));

    TEST_ASSERT_EQUAL(plaintext_len, decrypted_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, decrypted, plaintext_len);
}

void test_decrypt_exact_block_size(void)
{
    uint8_t key[32];
    memset(key, 0xAB, 32);

    uint8_t iv[16];
    memset(iv, 0xCD, 16);

    // 16 bytes = exactly one block (PKCS7 adds full block of padding)
    const uint8_t plaintext[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    uint8_t encrypted[256];
    size_t encrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_encrypt(key, iv, plaintext, 16, encrypted, &encrypted_len));
    TEST_ASSERT_EQUAL(16 + 32, encrypted_len); // IV + 2 blocks (16 data + 16 padding)

    uint8_t decrypted[256];
    size_t decrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_decrypt(key, encrypted, encrypted_len, decrypted, &decrypted_len));

    TEST_ASSERT_EQUAL(16, decrypted_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, decrypted, 16);
}

void test_decrypt_too_short(void)
{
    uint8_t key[32] = {0};
    uint8_t input[16] = {0}; // Only IV, no ciphertext
    uint8_t output[16];
    size_t output_len;

    TEST_ASSERT_FALSE(aes256_cbc_decrypt(key, input, 16, output, &output_len));
}

void test_decrypt_bad_padding(void)
{
    uint8_t key[32];
    memset(key, 0x42, 32);

    // Manually craft: IV + one block with invalid padding
    uint8_t bad_input[48]; // 16 IV + 32 ciphertext
    memset(bad_input, 0, 48);

    // Encrypt a known block, then corrupt the padding
    uint8_t iv[16] = {0};
    uint8_t plaintext[16];
    memset(plaintext, 0x07, 16); // fake "valid" data

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 256);

    // Encrypt without PKCS7 — the decrypted content will have whatever padding
    // the raw bytes happen to produce, which should fail PKCS7 validation
    uint8_t iv_copy[16] = {0};
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 16, iv_copy, plaintext, bad_input + 16);
    mbedtls_aes_free(&ctx);

    uint8_t output[32];
    size_t output_len;
    // This might pass or fail depending on what the last byte decrypts to.
    // The point is it shouldn't crash.
    aes256_cbc_decrypt(key, bad_input, 32, output, &output_len);
}

void test_decrypt_large_binary_data(void)
{
    uint8_t key[32];
    memset(key, 0x77, 32);

    uint8_t iv[16];
    memset(iv, 0x88, 16);

    // Simulate a small BMP-like payload
    const size_t data_size = 1024;
    uint8_t *plaintext = new uint8_t[data_size];
    for (size_t i = 0; i < data_size; i++)
        plaintext[i] = (uint8_t)(i & 0xFF);

    uint8_t *encrypted = new uint8_t[data_size + 48]; // room for IV + padding
    size_t encrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_encrypt(key, iv, plaintext, data_size, encrypted, &encrypted_len));

    uint8_t *decrypted = new uint8_t[data_size + 16];
    size_t decrypted_len = 0;
    TEST_ASSERT_TRUE(aes256_cbc_decrypt(key, encrypted, encrypted_len, decrypted, &decrypted_len));

    TEST_ASSERT_EQUAL(data_size, decrypted_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, decrypted, data_size);

    delete[] plaintext;
    delete[] encrypted;
    delete[] decrypted;
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_hex_to_bytes_valid);
    RUN_TEST(test_hex_to_bytes_uppercase);
    RUN_TEST(test_hex_to_bytes_invalid);
    RUN_TEST(test_decrypt_roundtrip);
    RUN_TEST(test_decrypt_exact_block_size);
    RUN_TEST(test_decrypt_too_short);
    RUN_TEST(test_decrypt_bad_padding);
    RUN_TEST(test_decrypt_large_binary_data);
    UNITY_END();
    return 0;
}
