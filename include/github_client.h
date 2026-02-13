#ifndef GITHUB_CLIENT_H
#define GITHUB_CLIENT_H

#include <cstdint>
#include <cstddef>

/**
 * @brief Download a file from an HTTPS URL into a PSRAM-allocated buffer
 * @param url Full HTTPS URL to download
 * @param out_size Pointer to store the downloaded data size
 * @return Pointer to PSRAM-allocated buffer (caller must free with free()), or nullptr on error
 */
uint8_t *https_download(const char *url, size_t *out_size);

#endif
