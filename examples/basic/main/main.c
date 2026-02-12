#include <stdio.h>
#include "cast_protocol.h"

void app_main(void)
{
    ESP_ERROR_CHECK(cast_protocol_init());

    // Your application code here

    ESP_ERROR_CHECK(cast_protocol_deinit());
}
