#include "cast_protocol.h"
#include "esp_log.h"

static const char *TAG = "cast_protocol";

esp_err_t cast_protocol_init(void)
{
    ESP_LOGI(TAG, "Initializing cast protocol");
    return ESP_OK;
}

esp_err_t cast_protocol_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing cast protocol");
    return ESP_OK;
}
