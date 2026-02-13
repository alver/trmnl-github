#ifndef CRYPTO_H
#define CRYPTO_H

#include <cstdint>
#include <cstddef>

#define AES256_KEY_SIZE 32
#define AES_BLOCK_SIZE 16
#define AES_IV_SIZE 16

/**
 * @brief Decrypt AES-256-CBC encrypted data with PKCS7 padding
 * @param key 32-byte AES key
 * @param input Input buffer: [16-byte IV][ciphertext]
 * @param input_len Total length of input (IV + ciphertext)
 * @param output Output buffer for decrypted plaintext (must be at least input_len - 16 bytes)
 * @param output_len Pointer to store actual output length after unpadding
 * @return true on success, false on error
 */
bool aes256_cbc_decrypt(const uint8_t *key, const uint8_t *input, size_t input_len,
                        uint8_t *output, size_t *output_len);

/**
 * @brief Parse a hex string into a byte array
 * @param hex Hex string (64 chars for 32 bytes)
 * @param out Output byte array
 * @param out_len Length of output array
 * @return true on success, false on error
 */
bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);

#endif
