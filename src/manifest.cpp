#include "manifest.h"
#include <ArduinoJson.h>
#include <trmnl_log.h>

bool parse_manifest(const uint8_t *json, size_t len, Manifest &out)
{
    if (!json || len == 0)
        return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (const char *)json, len);

    if (err)
    {
        Log_error("Manifest JSON parse error: %s", err.c_str());
        return false;
    }

    out.version = doc["version"] | 0;
    out.refresh_rate = doc["refresh_rate"] | 1800;
    out.updated_at = doc["updated_at"] | "";

    JsonArray screens = doc["screens"];
    if (screens.isNull())
    {
        Log_error("Manifest has no screens array");
        return false;
    }

    out.screen_count = 0;
    for (JsonObject screen : screens)
    {
        if (out.screen_count >= MANIFEST_MAX_SCREENS)
        {
            Log_info("Manifest: truncated at %d screens", MANIFEST_MAX_SCREENS);
            break;
        }

        ManifestScreen &s = out.screens[out.screen_count];
        s.name = screen["name"] | "";
        s.filename = screen["filename"] | "";
        s.size = screen["size"] | 0;
        out.screen_count++;
    }

    if (out.screen_count == 0)
    {
        Log_error("Manifest has no screens");
        return false;
    }

    Log_info("Manifest: v%d, %d screens, refresh %ds", out.version, out.screen_count, out.refresh_rate);
    return true;
}
