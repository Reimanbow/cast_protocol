/**
 * @file cast_chunker.c
 */
#include "cast_protocol.h"
#include "cast_internal.h"

#include <stdlib.h>
#include <string.h>

esp_err_t cast_send_frame(
    const uint8_t *data,
    size_t len,
    cast_image_format_t fmt,
    uint16_t width,
    uint16_t height,
    cast_transport_interface_t *transport
) {
    static uint16_t frame_counter = 0;
    const size_t mtu = transport->get_mtu();
    const size_t max_payload = mtu - CAST_PROTOCOL_OVERHEAD;

    // 総チャンク数の計算(切り上げ)
    uint16_t total_chunks = (len + max_payload - 1) / max_payload;

    // 送信用の一時バッファ(MTUサイズ分確保)
    uint8_t *packet_buf = (uint8_t *)malloc(mtu);
    if (!packet_buf) return ESP_ERR_NO_MEM;
    
    size_t sent_bytes = 0;

    for (uint16_t i = 0; i < total_chunks; i++) {
        // 残りデータ量と最大ペイロードの小さい方を今回のサイズにする
        size_t current_payload_len = (len - sent_bytes > max_payload) ? max_payload : (len - sent_bytes);

        // 1. ヘッダの組み立て
        cast_header_t *header = (cast_header_t *)packet_buf;
        header->magic = CAST_MAGIC_BYTE;
        header->type = CAST_PKT_TYPE_DATA;
        header->format = fmt;
        header->frame_id = frame_counter;
        header->chunk_index = i;
        header->total_chunks = total_chunks;
        header->max_payload = (uint16_t)max_payload;
        header->width = width;
        header->height = height;
        header->payload_len = (uint16_t)current_payload_len;
        
        // 2. データのコピー(ヘッダの直後へ)
        memcpy(packet_buf + sizeof(cast_header_t), data + sent_bytes, current_payload_len);

        // 3. トレーラ(CRC)の付与(今は0固定)
        uint16_t crc = 0;
        memcpy(packet_buf + sizeof(cast_header_t) + current_payload_len, &crc, CAST_CRC_SIZE);

        // 4. 送信
        size_t total_packet_len = sizeof(cast_header_t) + current_payload_len + CAST_CRC_SIZE;
        esp_err_t err = transport->send(packet_buf, total_packet_len);

        if (err != ESP_OK) {
            free(packet_buf);
            return err;
        }

        sent_bytes += current_payload_len;
    }

    free(packet_buf);
    frame_counter++;
    return ESP_OK;
}