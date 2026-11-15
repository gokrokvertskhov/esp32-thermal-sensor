#include "debug_console.h"
#include "board_i2c.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_heap_caps.h"

static const char *TAG = "debug_console";
static esp_console_repl_t *s_repl = NULL;

static int cmd_i2c_scan(int argc, char **argv)
{
    board_i2c_scan();
    return 0;
}

static int cmd_heap(int argc, char **argv)
{
    size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t min_free_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t min_free_psram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    printf("Heap Summary:\n");
    printf("  Internal DRAM: Free=%zu bytes, Min Free=%zu bytes\n", free_dram, min_free_dram);
    printf("  Octal PSRAM:   Free=%zu bytes, Min Free=%zu bytes\n", free_psram, min_free_psram);
    return 0;
}

static int cmd_log_level(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: log_level <tag> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    const char *tag = argv[1];
    const char *level_str = argv[2];
    esp_log_level_t level = ESP_LOG_INFO;

    if (strcasecmp(level_str, "none") == 0) level = ESP_LOG_NONE;
    else if (strcasecmp(level_str, "error") == 0) level = ESP_LOG_ERROR;
    else if (strcasecmp(level_str, "warn") == 0) level = ESP_LOG_WARN;
    else if (strcasecmp(level_str, "info") == 0) level = ESP_LOG_INFO;
    else if (strcasecmp(level_str, "debug") == 0) level = ESP_LOG_DEBUG;
    else if (strcasecmp(level_str, "verbose") == 0) level = ESP_LOG_VERBOSE;
    else {
        printf("Unknown log level '%s'\n", level_str);
        return 1;
    }

    esp_log_level_set(tag, level);
    printf("Set log level for '%s' to %s\n", tag, level_str);
    return 0;
}

esp_err_t debug_console_init(void)
{
    ESP_LOGI(TAG, "Initializing USB Serial/JTAG debug console REPL...");

    esp_console_dev_usb_serial_jtag_config_t dev_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "thermal> ";
    repl_config.max_cmdline_length = 256;
    repl_config.task_stack_size = 8192;

    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &s_repl),
                        TAG, "Failed to create console usb serial jtag repl");

    esp_console_register_help_command();

    const esp_console_cmd_t i2c_cmd = {
        .command = "i2c_scan",
        .help = "Scan shared I2C bus for connected slave devices",
        .hint = NULL,
        .func = &cmd_i2c_scan,
    };
    esp_console_cmd_register(&i2c_cmd);

    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Print internal SRAM and Octal PSRAM memory statistics",
        .hint = NULL,
        .func = &cmd_heap,
    };
    esp_console_cmd_register(&heap_cmd);

    const esp_console_cmd_t log_cmd = {
        .command = "log_level",
        .help = "Change runtime log level for a tag",
        .hint = "<tag> <none|error|warn|info|debug|verbose>",
        .func = &cmd_log_level,
    };
    esp_console_cmd_register(&log_cmd);

    ESP_RETURN_ON_ERROR(esp_console_start_repl(s_repl), TAG, "Failed to start console repl");
    ESP_LOGI(TAG, "Debug console REPL started");
    return ESP_OK;
}
