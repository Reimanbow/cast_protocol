/**
 * @file cast_itransport.h
 */
#ifndef CAST_ITRANSPORT_H
#define CAST_ITRANSPORT_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief CASTトランスポートインタフェース構造体
 * 各通信規格(WiFi, ESP-NOW等)はこの構造体の関数ポインタを実装する
 */
typedef struct {
    /**
     * @brief パケット送信関数
     * CASTミドルウェアがヘッダを付与し、分割した1チャンクを送信側に渡す
     * @param data 送信するデータのポインタ
     * @param len データ長
     * @return esp_err_t 送信成否(ESP_OK, ESP_FAILなど)
     */
    esp_err_t (*send)(const uint8_t *data, size_t len);

    /**
     * @brief 最大ペイロードサイズ(MTU)の取得
     * ミドルウェアはこの値を元に画像を分割するサイズを決定する
     * @return size_t 送信可能な最大バイト数
     */
    size_t (*get_mtu)(void);

    /**
     * @brief 現在のRSSIを取得する
     * @return int16_t RSSI(dBm)。取得不可なら0またはエラー値を返す
     */
    int16_t (*get_rssi)(void);

    /**
     * @brief 相手とのセッションが確立されているか
     * @return true 接続されている / false 接続されていない
     */
    bool (*is_connected)(void);

    /**
     * @brief 通信路の状態確認
     * 現在、送信可能な状態か（接続済みか等）を返す
     * @return true 送信可能 / false 送信不可
     */
    bool (*is_ready)(void);

    /**
     * @brief 受信コールバックの登録
     * 下層のドライバがデータを受信した際、ミドルウェアへ渡すための仕組み
     * @return esp_err_t 送信成否(ESP_OK, ESP_FAILなど)
     */
    esp_err_t (*set_recv_callback)(void (*cb)(const uint8_t *data, size_t len));
} cast_transport_interface_t;

#endif /* CAST_ITRANSPORT_H */