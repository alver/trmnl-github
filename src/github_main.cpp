#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WifiCaptive.h>
#include <pins.h>
#include <config.h>
#include <display.h>
#include <bmp.h>
#include <button.h>
#include <trmnl_log.h>
#include <crypto.h>
#include <github_client.h>
#include <manifest.h>
#include <api-client/display.h>  // for ApiDisplayResult type needed by display.cpp extern
#include <cstdarg>
#include <cstdio>
#include "logo_small.h"
#include "logo_medium.h"
#include "loading.h"
#include "secrets.h"

// ---- Globals required by display.cpp externs ----
Preferences preferences;
ApiDisplayResult apiDisplayResult;  // display.cpp references temp_profile, maximum_compatibility
char filename[1024];                // display.cpp extern

// ---- RTC memory (survives deep sleep) ----
RTC_DATA_ATTR uint8_t playlist_index = 0;
RTC_DATA_ATTR uint8_t need_to_refresh_display = 1;

// ---- NVS keys for our config ----
#define PREF_MANIFEST_URL    "manifest_url"
#define PREF_AES_KEY_HEX     "aes_key_hex"
#define PREF_IMAGES_BASE     "images_base"
#define PREF_WIFI_RETRY_COUNT "wifi_retry"   // progressive WiFi backoff counter
#define PREF_API_RETRY_COUNT  "api_retry"    // progressive download backoff counter


static unsigned long startup_time = 0;
static float vBatt = 4.2f;

// ---- Simple log_impl (replaces app_logger.cpp) ----
void log_impl(LogLevel level, LogMode mode, const char *file, int line, const char *format, ...)
{
    const int BUF_SIZE = 512;
    char buf[BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, BUF_SIZE, format, args);
    va_end(args);
    Serial.printf("%s [%d]: %s\r\n", file, line, buf);
}

// ---- Battery reading ----
static float readBatteryVoltage()
{
#ifdef FAKE_BATTERY_VOLTAGE
    return 4.2f;
#else
    // Simple ADC read — same pattern as stock firmware
    analogSetAttenuation(ADC_11db);
    int raw = analogRead(PIN_BATTERY);
    return (raw / 4095.0f) * 3.3f * 2.0f;  // voltage divider factor
#endif
}

// ---- Deep sleep ----
static void goToSleep(uint32_t sleep_seconds)
{
    if (WiFi.status() == WL_CONNECTED)
        WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    Log_info("Total awake time: %d ms", millis() - startup_time);
    Log_info("Sleeping for %d seconds", sleep_seconds);

    preferences.putUInt(PREFERENCES_SLEEP_TIME_KEY, sleep_seconds);
    preferences.end();

    esp_sleep_enable_timer_wakeup((uint64_t)sleep_seconds * SLEEP_uS_TO_S_FACTOR);

    // Configure GPIO wakeup per chip target
#if CONFIG_IDF_TARGET_ESP32
    #define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
    esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(PIN_INTERRUPT), ESP_EXT1_WAKEUP_ALL_LOW);
#elif CONFIG_IDF_TARGET_ESP32C3
    esp_deep_sleep_enable_gpio_wakeup(1 << PIN_INTERRUPT, ESP_GPIO_WAKEUP_GPIO_LOW);
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_INTERRUPT, 0);
#else
    #error "Unsupported ESP32 target for GPIO wakeup"
#endif

    esp_deep_sleep_start();
}

// ---- Factory reset: wipe all credentials and restart ----
// Safe to call at any point — uses its own local Preferences handle so it
// works whether the global 'preferences' object is open (portal callback path)
// or not yet opened (SoftReset button path, before preferences.begin()).
static void resetDeviceCredentials()
{
    Log_info("Factory reset: clearing WiFi and NVS, restarting");
    WifiCaptivePortal.resetSettings();
    Preferences prefs;
    if (prefs.begin("data", false))
    {
        prefs.clear();
        prefs.end();
    }
    ESP.restart();
}

// ---- Show error on display and sleep (fixed duration, for config/decrypt errors) ----
static void errorAndSleep(MSG msg, uint32_t sleep_seconds)
{
    display_show_msg(const_cast<uint8_t *>(logo_medium), msg);
    display_sleep();
    goToSleep(sleep_seconds);
}

// ---- WiFi failure with progressive backoff ----
// Retry schedule: 60s → 180s → 300s → 900s (normal interval)
// Counter stored in NVS; reset to 1 on successful WiFi connect.
static void wifiErrorAndSleep(MSG msg)
{
    int retries = preferences.getInt(PREF_WIFI_RETRY_COUNT, 1);
    uint32_t sleep_secs;
    switch (retries)
    {
    case 1:  sleep_secs = 60;                break;
    case 2:  sleep_secs = 180;               break;
    case 3:  sleep_secs = 300;               break;
    default: sleep_secs = SLEEP_TIME_TO_SLEEP; break;
    }
    Log_error("WiFi failed (attempt %d), sleeping %ds", retries, sleep_secs);
    preferences.putInt(PREF_WIFI_RETRY_COUNT, retries + 1);
    display_show_msg(const_cast<uint8_t *>(logo_medium), msg);
    display_sleep();
    goToSleep(sleep_secs);
}

// ---- Download/network failure with progressive backoff ----
// Retry schedule: 15s → 30s → 60s → 900s (normal interval)
// Counter stored in NVS; reset to 1 on successful image display.
static void downloadErrorAndSleep(MSG msg)
{
    int retries = preferences.getInt(PREF_API_RETRY_COUNT, 1);
    uint32_t sleep_secs;
    switch (retries)
    {
    case 1:  sleep_secs = 15;                break;
    case 2:  sleep_secs = 30;                break;
    case 3:  sleep_secs = 60;                break;
    default: sleep_secs = SLEEP_TIME_TO_SLEEP; break;
    }
    Log_error("Download failed (attempt %d), sleeping %ds", retries, sleep_secs);
    preferences.putInt(PREF_API_RETRY_COUNT, retries + 1);
    display_show_msg(const_cast<uint8_t *>(logo_medium), msg);
    display_sleep();
    goToSleep(sleep_secs);
}

// ---- Main setup (runs on every wake) ----
void setup()
{
    startup_time = millis();
    Serial.begin(115200);

#ifdef WAIT_FOR_SERIAL
    unsigned long start = millis();
    while (millis() - start < 2000)
    {
        if (Serial) break;
        delay(100);
    }
#endif

    Log_info("GitHub Pages firmware starting");
    Log_info("FW version %s", FW_VERSION_STRING);

    pins_init();
    vBatt = readBatteryVoltage();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Tracks whether a DoubleClick occurred so we can clear download backoff
    // after preferences.begin() (preferences is not open yet during button reading).
    bool double_clicked = false;

    // Handle button presses on GPIO wakeup
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT1)
    {
        Log_info("GPIO wakeup detected");
        auto button = read_button_presses();

#ifdef WAIT_FOR_SERIAL
        // read_button_presses() blocks for the full press duration (up to 15s for
        // SoftReset). USB CDC serial may not be attached at wakeup but will be by
        // the time the button is released. Wait here so the button-result log and
        // every subsequent line are captured — the top-of-setup wait may have already
        // expired during the button read.
        {
            unsigned long ws = millis();
            while (!Serial && millis() - ws < 2000) { delay(100); }
        }
#endif

        Log_info("Button result: %d", (int)button);
        switch (button)
        {
        case LongPress:
            Log_info("Long press: resetting WiFi credentials");
            WifiCaptivePortal.resetSettings();
            break;
        case DoubleClick:
        {
            // Advance playlist so this wake shows the screen AFTER the one that
            // would normally have been displayed. playlist_index++ is safe: uint8_t
            // wraps 255→0 and the clamp below (playlist_index >= screen_count → 0)
            // handles any value correctly. Do NOT use % 255 — that maps 254→0.
            uint8_t prev = playlist_index;
            playlist_index++;
            double_clicked = true;
            Log_info("Double click: playlist index %d → %d (clamped after manifest load)",
                     prev, playlist_index);
            break;
        }
        case SoftReset:
            Log_info("Soft reset: factory resetting device");
            resetDeviceCredentials();  // does not return
            break;
        default:
            break;
        }
    }

    // Init preferences
    if (!preferences.begin("data", false))
    {
        Log_fatal("Preferences init failed");
        ESP.restart();
    }

    // DoubleClick: clear any accumulated download backoff so the user-requested
    // refresh isn't delayed by a previous failure's retry counter.
    if (double_clicked)
    {
        preferences.putInt(PREF_API_RETRY_COUNT, 1);
        Log_info("Double click: download retry counter reset");
    }

    // Init display
    display_init();

    // Show loading screen only on GPIO wakeup (button press) or first boot.
    // Timer wakeups skip straight to download — no extra render means the
    // partial-refresh ghost counter advances once per cycle, not twice.
    // Always wait (bWait=true) so the EPD finishes before WiFi/download starts;
    // sending a second render while the panel is still physically refreshing
    // causes ghost images from the loading screen bleeding into the content.
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO  ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT0  ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT1  ||
        wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        display_show_image(const_cast<uint8_t *>(logo_medium), DEFAULT_IMAGE_SIZE, true);
        need_to_refresh_display = 1;
    }

    // ---- WiFi connect ----
    WiFi.mode(WIFI_STA);

    if (WifiCaptivePortal.isSaved())
    {
        Log_info("WiFi saved, auto-connecting");
        if (!WifiCaptivePortal.autoConnect())
        {
            Log_error("WiFi connection failed");
            wifiErrorAndSleep(WIFI_FAILED);  // does not return
        }
        Log_info("WiFi connected: %s", WiFi.localIP().toString().c_str());
        preferences.putInt(PREF_WIFI_RETRY_COUNT, 1);  // reset backoff on success
    }
    else
    {
        Log_info("No WiFi saved, starting captive portal");
        display_show_msg(const_cast<uint8_t *>(logo_medium), WIFI_CONNECT,
                         "", false, FW_VERSION_STRING, "");
        WifiCaptivePortal.setResetSettingsCallback(resetDeviceCredentials);
        if (!WifiCaptivePortal.startPortal())
        {
            wifiErrorAndSleep(WIFI_FAILED);  // does not return
        }
        Log_info("WiFi connected via portal");
        preferences.putInt(PREF_WIFI_RETRY_COUNT, 1);  // reset backoff on success
    }

    // ---- NTP clock sync (best-effort) ----
    // Not required for HTTPS — setInsecure() skips cert date validation — but
    // corrects log timestamps and future-proofs against pinned certificates.
    // 2s timeout; failure is logged but does not block the main flow.
    configTime(0, 0, "time.google.com", "time.cloudflare.com");
    {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 2000))
            Log_info("NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        else
            Log_info("NTP sync timed out — continuing with system clock");
    }

    // ---- Load config from NVS ----
    String manifest_url = preferences.getString(PREF_MANIFEST_URL, GITHUB_PAGES_MANIFEST_URL);
    String images_base = preferences.getString(PREF_IMAGES_BASE, GITHUB_PAGES_IMAGES_BASE);
    String aes_key_hex = preferences.getString(PREF_AES_KEY_HEX, GITHUB_PAGES_AES_KEY_HEX);

    uint8_t aes_key[AES256_KEY_SIZE];
    if (!hex_to_bytes(aes_key_hex.c_str(), aes_key, AES256_KEY_SIZE))
    {
        Log_fatal("Invalid AES key hex in NVS");
        errorAndSleep(API_ERROR, 300);
    }

    // ---- Fetch and decrypt manifest ----
    Log_info("Free heap before download: %d bytes (largest block: %d)",
             ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Log_info("Fetching manifest: %s", manifest_url.c_str());
    size_t manifest_enc_size = 0;
    uint8_t *manifest_enc = https_download(manifest_url.c_str(), &manifest_enc_size);
    if (!manifest_enc)
    {
        Log_error("Failed to download manifest");
        downloadErrorAndSleep(API_UNABLE_TO_CONNECT);  // does not return
    }

    // Decrypt manifest
    size_t manifest_dec_size = 0;
    uint8_t *manifest_dec = (uint8_t *)malloc(manifest_enc_size);
    if (!manifest_dec)
    {
        free(manifest_enc);
        Log_error("Failed to allocate manifest decrypt buffer");
        errorAndSleep(API_ERROR, 60);
    }

    if (!aes256_cbc_decrypt(aes_key, manifest_enc, manifest_enc_size, manifest_dec, &manifest_dec_size))
    {
        free(manifest_enc);
        free(manifest_dec);
        Log_error("Failed to decrypt manifest");
        errorAndSleep(API_ERROR, 300);
    }
    free(manifest_enc);

    // Parse manifest
    Manifest manifest;
    if (!parse_manifest(manifest_dec, manifest_dec_size, manifest))
    {
        free(manifest_dec);
        Log_error("Failed to parse manifest");
        errorAndSleep(API_ERROR, 300);
    }
    free(manifest_dec);

    Log_info("Manifest: %d screens, refresh_rate=%d", manifest.screen_count, manifest.refresh_rate);

    // ---- Select screen from playlist ----
    if (playlist_index >= manifest.screen_count)
        playlist_index = 0;

    ManifestScreen &screen = manifest.screens[playlist_index];
    Log_info("Screen %d/%d: %s (%s)", playlist_index + 1, manifest.screen_count,
             screen.name.c_str(), screen.filename.c_str());

    // Advance playlist for next wake
    playlist_index = (playlist_index + 1) % manifest.screen_count;

    // ---- Download encrypted image ----
    String image_url = images_base + screen.filename;
    Log_info("Fetching image: %s", image_url.c_str());

    size_t image_enc_size = 0;
    uint8_t *image_enc = https_download(image_url.c_str(), &image_enc_size);
    if (!image_enc)
    {
        Log_error("Failed to download image");
        downloadErrorAndSleep(API_IMAGE_DOWNLOAD_ERROR);  // does not return
    }

    // Done with WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // ---- Decrypt image ----
    size_t image_dec_size = 0;
    uint8_t *image_dec = (uint8_t *)heap_caps_malloc(image_enc_size, MALLOC_CAP_SPIRAM);
    if (!image_dec)
    {
        image_dec = (uint8_t *)malloc(image_enc_size);
    }
    if (!image_dec)
    {
        free(image_enc);
        Log_error("Failed to allocate image decrypt buffer");
        errorAndSleep(API_ERROR, 60);
    }

    if (!aes256_cbc_decrypt(aes_key, image_enc, image_enc_size, image_dec, &image_dec_size))
    {
        free(image_enc);
        free(image_dec);
        Log_error("Failed to decrypt image");
        errorAndSleep(API_ERROR, 300);
    }
    free(image_enc);

    // ---- Detect format and display image ----
    // display_show_image() does its own magic-byte detection internally (PNG/JPEG/
    // BMP/G5). We pre-check here to: (a) validate BMP headers for a clear error
    // message, and (b) reject completely unknown formats before the display driver
    // sees them.
    if (image_dec_size < 4)
    {
        Log_error("Image too small to detect format: %d bytes", image_dec_size);
        free(image_dec);
        errorAndSleep(MSG_FORMAT_ERROR, 300);
    }

    bool is_bmp  = (image_dec[0] == 'B'  && image_dec[1] == 'M');
    bool is_png  = (image_dec[0] == 0x89 && image_dec[1] == 0x50);  // PNG magic
    bool is_jpeg = (image_dec[0] == 0xFF && image_dec[1] == 0xD8);  // JPEG SOI

    if (is_bmp)
    {
        // Validate BMP header: dimensions must be 800x480, 1-bpp, correct color table.
        // parseBMPHeader() also sets image_reverse if the color table is inverted.
        bool image_reverse = false;
        bmp_err_e bmp_res = parseBMPHeader(image_dec, image_reverse);
        if (bmp_res != BMP_NO_ERR)
        {
            Log_error("BMP header invalid (error %d)", bmp_res);
            free(image_dec);
            errorAndSleep(MSG_FORMAT_ERROR, 300);
        }
    }
    else if (!is_png && !is_jpeg)
    {
        Log_error("Unknown image format (magic: %02x %02x)", image_dec[0], image_dec[1]);
        free(image_dec);
        errorAndSleep(MSG_FORMAT_ERROR, 300);
    }

    Log_info("Displaying %s image (%d bytes)",
             is_bmp ? "BMP" : is_png ? "PNG" : "JPEG", image_dec_size);
    display_show_image(image_dec, image_dec_size, true);
    free(image_dec);

    // Both counters reset — full successful cycle completed
    preferences.putInt(PREF_API_RETRY_COUNT, 1);
    need_to_refresh_display = 0;

    // ---- Sleep ----
    display_sleep();
    goToSleep(manifest.refresh_rate);
}

void loop()
{
    // Never reached — deep sleep restarts from setup()
}
