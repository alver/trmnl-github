#include "github_client.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <trmnl_log.h>
#include <esp_heap_caps.h>

uint8_t *https_download(const char *url, size_t *out_size)
{
    if (!url || !out_size)
        return nullptr;

    *out_size = 0;

    WiFiClientSecure *client = new WiFiClientSecure();
    if (!client)
    {
        Log_error("Failed to create WiFiClientSecure");
        return nullptr;
    }
    client->setInsecure(); // TODO: pin GitHub Pages root CA cert

    HTTPClient https;
    if (!https.begin(*client, url))
    {
        Log_error("HTTPClient begin failed for %s", url);
        delete client;
        return nullptr;
    }

    https.setTimeout(15000);
    https.setConnectTimeout(15000);
    https.setReuse(false);

    int httpCode = https.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Log_error("HTTP GET failed: %d %s", httpCode, https.errorToString(httpCode).c_str());
        https.end();
        client->stop();
        delete client;
        return nullptr;
    }

    int content_size = https.getSize();
    Log_info("Download %s: %d bytes", url, content_size);

    // Use getString() which handles chunked transfer encoding
    String payload = https.getString();
    size_t data_len = payload.length();

    https.end();
    client->stop();
    delete client;

    if (data_len == 0)
    {
        Log_error("Empty response from %s", url);
        return nullptr;
    }

    // Allocate in PSRAM
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(data_len, MALLOC_CAP_SPIRAM);
    if (!buffer)
    {
        // Fall back to regular heap if no PSRAM
        Log_info("PSRAM alloc failed, trying regular heap");
        buffer = (uint8_t *)malloc(data_len);
    }

    if (!buffer)
    {
        Log_error("Failed to allocate %d bytes for download buffer", data_len);
        return nullptr;
    }

    memcpy(buffer, payload.c_str(), data_len);
    *out_size = data_len;

    Log_info("Downloaded %d bytes from %s", data_len, url);
    return buffer;
}
