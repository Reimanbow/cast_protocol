/**
 * @file cast_protocol.h
 */
#ifndef CAST_PROTOCOL_H
#define CAST_PROTOCOL_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#include "cast_itransport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 送信する画像のフォーマット情報
 */
typedef enum {
    CAST_FMT_JPEG       = 0,
    CAST_FMT_RGB565     = 1,
    CAST_FMT_GRAYSCALE  = 2,
    CAST_FMT_NONE       = 255   // 制御パケット用
} cast_image_format_t;

/**
 * @brief 画像を複数チャンクに分割し送信する
 * MTUは各無線通信規格によりある程度決められている
 * @param data 送信するデータのポインタ
 * @param len データ長
 * @param fmt 画像のフォーマット
 * @param transport 無線規格の振る舞い
 * @return esp_err_t 送信成否(ESP_OK, ESP_FAILなど)
 */
esp_err_t cast_send_frame(
    const uint8_t *data,
    size_t len,
    cast_image_format_t fmt,
    cast_transport_interface_t *transport
);

#ifdef __cplusplus
}
#endif

#endif /* CAST_PROTOCOL_H */