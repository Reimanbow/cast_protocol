/**
 * @file cast_reassembler.c
 */
#include "cast_protocol.h"
#include "cast_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 受信機内部で持つ情報
 */
typedef struct {
    uint8_t *buffer;            // 組み立て用バッファ
    size_t allocated_size;      // 現在確保しているサイズ
    uint16_t current_frame_id;  // 追跡中のフレームID
    uint16_t chunks_received;   // 到着済みチャンク数
    uint16_t total_chunks;      // 期待される総数
    uint16_t max_payload_ref;   // オフセット計算の基準値
    cast_image_format_t format; // 画像フォーマット
    bool is_active;
} cast_reassembler_ctx_t;

static cast_reassembler_ctx_t ctx = {0};
static cast_on_frame_ready_t app_callback = NULL;

// 前方宣言
static void cast_reassembler_push_packet(const uint8_t *data, size_t len);

void cast_reassembler_reset(void) {
    if (ctx.buffer) {
        free(ctx.buffer);
        ctx.buffer = NULL;
    }
    ctx.is_active = false;
    ctx.chunks_received = 0;
}

static void cast_reassembler_init(cast_on_frame_ready_t callback) {
    app_callback = callback;
    cast_reassembler_reset();
}

esp_err_t cast_init_receiver(cast_transport_interface_t *transport, cast_on_frame_ready_t callback) {
    cast_reassembler_init(callback);
    return transport->set_recv_callback(cast_reassembler_push_packet);
}

static void cast_reassembler_push_packet(const uint8_t *data, size_t len) {
    // 最小サイズチェック (Header + CRC)
    if (len < sizeof(cast_header_t) + CAST_CRC_SIZE) return;

    cast_header_t *header = (cast_header_t *)data;
    if (header->magic != CAST_MAGIC_BYTE) return;

    // TODO: CRC検証
    // uint16_t received_crc;
    // memcpy(&received_crc, data + sizeof(cast_header_t) + header->payload_len, CAST_CRC_SIZE);
    // if (calc_crc(data, len - CAST_CRC_SIZE) != received_crc) return;

    // 1. 新しいフレームの開始判断
    if (!ctx.is_active || header->frame_id != ctx.current_frame_id) {
        // 前の未完成のフレームがあればリセット
        cast_reassembler_reset();

        // メモリ確保: 安全のため(総数*最大サイズ)で確保
        size_t reserve_size = header->total_chunks * header->max_payload;
        ctx.buffer = (uint8_t *)malloc(reserve_size);
        if (!ctx.buffer) return;

        ctx.allocated_size = reserve_size;
        ctx.current_frame_id = header->frame_id;
        ctx.total_chunks = header->total_chunks;
        ctx.max_payload_ref = header->max_payload;
        ctx.format = (cast_image_format_t)header->format;
        ctx.chunks_received = 0;
        ctx.is_active = true;
    }

    // 2. オフセット計算と書き込み(ダイレクトマッピング)
    size_t offset = header->chunk_index * ctx.max_payload_ref;
    const uint8_t *payload = data + sizeof(cast_header_t);

    if (offset + header->payload_len <= ctx.allocated_size) {
        memcpy(ctx.buffer + offset, payload, header->payload_len);
        ctx.chunks_received++;
    }

    if (ctx.chunks_received == ctx.total_chunks) {
        if (app_callback) {
            size_t final_size = offset + header->payload_len;
            app_callback(ctx.buffer, final_size, ctx.format);
        }

        // callback から戻ったらミドルウェア側で解放
        // アプリはcallback内で必要ならコピーすること
        free(ctx.buffer);
        ctx.buffer = NULL;
        ctx.is_active = false;
    }
}
