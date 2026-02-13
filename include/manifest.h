#ifndef MANIFEST_H
#define MANIFEST_H

#include <Arduino.h>

#define MANIFEST_MAX_SCREENS 16

struct ManifestScreen
{
    String name;
    String filename;
    size_t size;
};

struct Manifest
{
    int version;
    int refresh_rate;
    String updated_at;
    int screen_count;
    ManifestScreen screens[MANIFEST_MAX_SCREENS];
};

/**
 * @brief Parse decrypted manifest JSON into a Manifest struct
 * @param json Pointer to JSON string (null-terminated not required)
 * @param len Length of JSON data
 * @param out Output Manifest struct
 * @return true on success, false on parse error
 */
bool parse_manifest(const uint8_t *json, size_t len, Manifest &out);

#endif
