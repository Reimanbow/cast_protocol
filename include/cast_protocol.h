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
 * @param width 画像の幅(px)。JPEGでは0可
 * @param height 画像の高さ(px)。JPEGでは0可
 * @param transport 無線規格の振る舞い
 * @return esp_err_t 送信成否(ESP_OK, ESP_FAILなど)
 */
esp_err_t cast_send_frame(
    const uint8_t *data,
    size_t len,
    cast_image_format_t fmt,
    uint16_t width,
    uint16_t height,
    cast_transport_interface_t *transport
);

/**
 * @brief 画像が完成したときに呼ばれる関数の型
 * @note dataはcallbackから戻った後にミドルウェアが解放する。保持する場合はコピーすること。
 * @param data 復元された画像データ
 * @param len データ長
 * @param fmt 画像フォーマット
 * @param width 画像の幅(px)
 * @param height 画像の高さ(px)
 */
typedef void (*cast_on_frame_ready_t)(const uint8_t *data, size_t len, cast_image_format_t fmt, uint16_t width, uint16_t height);

/**
 * @brief 受信側の初期化
 * transportのrecvコールバックを登録し、受信したパケットをreassemblerに渡す
 * @param transport トランスポートインタフェース
 * @param callback フレーム完成時に呼ばれるコールバック
 * @return esp_err_t ESP_OK on success
 */
esp_err_t cast_init_receiver(cast_transport_interface_t *transport, cast_on_frame_ready_t callback);

/**
 * @brief 受信中のフレーム組み立てをリセットする
 * 途中のフレームを破棄してバッファを解放する
 */
void cast_reassembler_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CAST_PROTOCOL_H */