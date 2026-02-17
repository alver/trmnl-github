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

    if (content_size <= 0)
    {
        Log_error("Invalid content size %d from %s", content_size, url);
        https.end();
        client->stop();
        delete client;
        return nullptr;
    }

    // Allocate output buffer first — in PSRAM if available, else regular heap
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(content_size, MALLOC_CAP_SPIRAM);
    if (!buffer)
    {
        Log_info("PSRAM alloc failed, trying regular heap");
        buffer = (uint8_t *)malloc(content_size);
    }
    if (!buffer)
    {
        Log_error("Failed to allocate %d bytes for download buffer", content_size);
        https.end();
        client->stop();
        delete client;
        return nullptr;
    }

    // Stream directly into buffer to avoid double allocation from getString()
    WiFiClient *stream = https.getStreamPtr();
    size_t bytes_read = 0;
    while (bytes_read < (size_t)content_size && stream->connected())
    {
        size_t available = stream->available();
        if (available)
        {
            size_t to_read = min(available, (size_t)content_size - bytes_read);
            size_t got = stream->readBytes(buffer + bytes_read, to_read);
            bytes_read += got;
        }
        else
        {
            delay(1); // yield to system tasks
        }
    }

    https.end();
    client->stop();
    delete client;

    if (bytes_read == 0)
    {
        Log_error("Empty response from %s", url);
        free(buffer);
        return nullptr;
    }

    *out_size = bytes_read;
    Log_info("Downloaded %d bytes from %s", bytes_read, url);
    return buffer;
}
