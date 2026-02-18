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
#define PREF_MANIFEST_URL "manifest_url"
#define PREF_AES_KEY_HEX  "aes_key_hex"
#define PREF_IMAGES_BASE  "images_base"


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

// ---- Show error on display and sleep ----
static void errorAndSleep(MSG msg, uint32_t sleep_seconds)
{
    display_show_msg(const_cast<uint8_t *>(logo_medium), msg);
    display_sleep();
    goToSleep(sleep_seconds);
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

    // Handle button presses on GPIO wakeup
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 ||
        wakeup_reason == ESP_SLEEP_WAKEUP_EXT1)
    {
        Log_info("GPIO wakeup detected");
        auto button = read_button_presses();
        switch (button)
        {
        case LongPress:
            Log_info("Long press: resetting WiFi credentials");
            WifiCaptivePortal.resetSettings();
            break;
        case DoubleClick:
            // Advance to next screen in playlist immediately
            Log_info("Double click: advancing playlist index");
            playlist_index = (playlist_index + 1) % 255;  // clamped later vs screen_count
            break;
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
            display_show_msg(const_cast<uint8_t *>(logo_medium), WIFI_FAILED);
            display_sleep();
            goToSleep(60); // retry in 1 minute
        }
        Log_info("WiFi connected: %s", WiFi.localIP().toString().c_str());
    }
    else
    {
        Log_info("No WiFi saved, starting captive portal");
        display_show_msg(const_cast<uint8_t *>(logo_medium), WIFI_CONNECT,
                         "", false, FW_VERSION_STRING, "");
        WifiCaptivePortal.setResetSettingsCallback(resetDeviceCredentials);
        if (!WifiCaptivePortal.startPortal())
        {
            WiFi.disconnect(true);
            display_show_msg(const_cast<uint8_t *>(logo_medium), WIFI_FAILED);
            display_sleep();
            goToSleep(60);
        }
        Log_info("WiFi connected via portal");
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
        errorAndSleep(API_UNABLE_TO_CONNECT, 60);
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
        WiFi.disconnect(true);
        errorAndSleep(API_IMAGE_DOWNLOAD_ERROR, 60);
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

    // ---- Parse and display BMP ----
    bool image_reverse = false;
    bmp_err_e bmp_res = parseBMPHeader(image_dec, image_reverse);

    if (bmp_res != BMP_NO_ERR)
    {
        Log_error("BMP parse error: %d", bmp_res);
        free(image_dec);
        errorAndSleep(MSG_FORMAT_ERROR, 300);
    }

    Log_info("Displaying image (%d bytes)", image_dec_size);
    display_show_image(image_dec, image_dec_size, true);
    free(image_dec);

    need_to_refresh_display = 0;

    // ---- Sleep ----
    display_sleep();
    goToSleep(manifest.refresh_rate);
}

void loop()
{
    // Never reached — deep sleep restarts from setup()
}
