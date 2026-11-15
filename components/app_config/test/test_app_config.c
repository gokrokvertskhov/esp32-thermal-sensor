#include "unity.h"
#include "app_config.h"
#include <string.h>

TEST_CASE("app_config_load_from_str valid config", "[app_config]")
{
    const char *valid_json =
        "{\n"
        "  \"wifi\": {\n"
        "    \"ssid\": \"TestAP\",\n"
        "    \"password\": \"secret123\",\n"
        "    \"hostname\": \"my-thermal\"\n"
        "  },\n"
        "  \"sensor\": {\n"
        "    \"refresh_rate_hz\": 8,\n"
        "    \"emissivity\": 0.90,\n"
        "    \"auto_start_continuous\": false\n"
        "  },\n"
        "  \"time\": {\n"
        "    \"ntp_server\": \"time.google.com\",\n"
        "    \"timezone\": \"UTC0\"\n"
        "  }\n"
        "}";

    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_OK, app_config_load_from_str(valid_json, &cfg));
    TEST_ASSERT_EQUAL_STRING("TestAP", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", cfg.wifi.password);
    TEST_ASSERT_EQUAL_STRING("my-thermal", cfg.wifi.hostname);
    TEST_ASSERT_EQUAL_STRING(APP_CONFIG_DEFAULT_SENSOR_MODEL, cfg.sensor.model);
    TEST_ASSERT_EQUAL_UINT8(8, cfg.sensor.refresh_rate_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, cfg.sensor.emissivity);
    TEST_ASSERT_FALSE(cfg.sensor.auto_start_continuous);
    TEST_ASSERT_EQUAL_STRING("time.google.com", cfg.time.ntp_server);
    TEST_ASSERT_EQUAL_STRING("UTC0", cfg.time.timezone);
}

TEST_CASE("app_config_load_from_str missing ssid", "[app_config]")
{
    const char *no_ssid = "{\"wifi\": {\"password\": \"12345678\"}}";
    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_config_load_from_str(no_ssid, &cfg));
}

TEST_CASE("app_config_load_from_str empty ssid", "[app_config]")
{
    const char *empty_ssid = "{\"wifi\": {\"ssid\": \"\"}}";
    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_config_load_from_str(empty_ssid, &cfg));
}

TEST_CASE("app_config_load_from_str malformed json", "[app_config]")
{
    const char *bad_json = "{\"wifi\": {broken";
    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, app_config_load_from_str(bad_json, &cfg));
}

TEST_CASE("app_config_load_from_str defaults for optional fields", "[app_config]")
{
    const char *minimal = "{\"wifi\": {\"ssid\": \"OnlySSID\"}}";
    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_OK, app_config_load_from_str(minimal, &cfg));
    TEST_ASSERT_EQUAL_STRING("OnlySSID", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("", cfg.wifi.password);
    TEST_ASSERT_EQUAL_STRING("thermal-server", cfg.wifi.hostname);
    TEST_ASSERT_EQUAL_STRING(APP_CONFIG_DEFAULT_SENSOR_MODEL, cfg.sensor.model);
    TEST_ASSERT_EQUAL_UINT8(4, cfg.sensor.refresh_rate_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, cfg.sensor.emissivity);
    TEST_ASSERT_TRUE(cfg.sensor.auto_start_continuous);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", cfg.time.ntp_server);
    TEST_ASSERT_EQUAL_STRING("PST8PDT,M3.2.0,M11.1.0", cfg.time.timezone);
}

TEST_CASE("app_config_load_from_str explicit sensor model", "[app_config]")
{
    const char *json_640 = "{\"wifi\": {\"ssid\": \"AP\"}, \"sensor\": {\"model\": \"mlx90640\"}}";
    app_config_t cfg;
    TEST_ASSERT_EQUAL(ESP_OK, app_config_load_from_str(json_640, &cfg));
    TEST_ASSERT_EQUAL_STRING("mlx90640", cfg.sensor.model);
}
