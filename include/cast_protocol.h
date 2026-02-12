#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the cast protocol
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 */
esp_err_t cast_protocol_init(void);

/**
 * @brief Deinitialize the cast protocol
 *
 * @return
 *      - ESP_OK on success
 */
esp_err_t cast_protocol_deinit(void);

#ifdef __cplusplus
}
#endif
