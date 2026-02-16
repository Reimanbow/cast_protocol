/**
 * @file cast_reassembler.c
 */
#include "cast_protocol.h"
#include "cast_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// BMP ヘッダ生成 (RGB565用)
// ---------------------------------------------------------------------------

// BMP with BI_BITFIELDS: FileHeader(14) + InfoHeader(40) + ColorMasks(12) = 66 bytes
#define BMP_HEADER_SIZE 66

#pragma pack(push, 1)
typedef struct {
    // File Header (14 bytes)
    uint8_t  signature[2];   // "BM"
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
    // Info Header - BITMAPINFOHEADER (40 bytes)
    uint32_t info_size;      // 40
    int32_t  width;
    int32_t  height;         // 負値 = top-down
    uint16_t planes;         // 1
    uint16_t bpp;            // 16
    uint32_t compression;    // 3 = BI_BITFIELDS
    uint32_t image_size;
    int32_t  x_ppm;          // pixels per meter
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
    // Color masks (12 bytes)
    uint32_t mask_r;         // 0xF800
    uint32_t mask_g;         // 0x07E0
    uint32_t mask_b;         // 0x001F
} bmp_header_t;
#pragma pack(pop)

/**
 * @brief RGB565 生ピクセルデータに BMP ヘッダを付与した完成ファイルを生成
 * @param pixels 生ピクセルデータ
 * @param pixel_len ピクセルデータのバイト数
 * @param w 画像幅
 * @param h 画像高さ
 * @param out_len 出力ファイルサイズ
 * @return malloc されたファイルデータ。呼び出し側が free する。失敗時 NULL
 */
static uint8_t *build_bmp_rgb565(const uint8_t *pixels, size_t pixel_len,
                                  uint16_t w, uint16_t h, size_t *out_len)
{
    // BMP の行は4バイト境界にパディング
    uint32_t row_stride = ((w * 2 + 3) / 4) * 4;
    uint32_t image_size = row_stride * h;
    uint32_t file_size = BMP_HEADER_SIZE + image_size;

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) return NULL;
    memset(buf, 0, file_size);

    bmp_header_t *bmp = (bmp_header_t *)buf;
    bmp->signature[0]    = 'B';
    bmp->signature[1]    = 'M';
    bmp->file_size       = file_size;
    bmp->data_offset     = BMP_HEADER_SIZE;
    bmp->info_size       = 40;
    bmp->width           = w;
    bmp->height          = -(int32_t)h;  // top-down
    bmp->planes          = 1;
    bmp->bpp             = 16;
    bmp->compression     = 3; // BI_BITFIELDS
    bmp->image_size      = image_size;
    bmp->mask_r          = 0xF800;
    bmp->mask_g          = 0x07E0;
    bmp->mask_b          = 0x001F;

    // 行ごとにコピー（パディング考慮）
    uint32_t src_row_bytes = w * 2;
    for (uint16_t y = 0; y < h; y++) {
        size_t src_offset = y * src_row_bytes;
        size_t dst_offset = BMP_HEADER_SIZE + y * row_stride;
        size_t copy_len = src_row_bytes;
        if (src_offset + copy_len > pixel_len) {
            copy_len = (src_offset < pixel_len) ? (pixel_len - src_offset) : 0;
        }
        if (copy_len > 0) {
            memcpy(buf + dst_offset, pixels + src_offset, copy_len);
        }
    }

    *out_len = file_size;
    return buf;
}

// ---------------------------------------------------------------------------
// PGM ヘッダ生成 (Grayscale用)
// ---------------------------------------------------------------------------

/**
 * @brief Grayscale 生ピクセルデータに PGM (P5) ヘッダを付与した完成ファイルを生成
 * @param pixels 生ピクセルデータ
 * @param pixel_len ピクセルデータのバイト数
 * @param w 画像幅
 * @param h 画像高さ
 * @param out_len 出力ファイルサイズ
 * @return malloc されたファイルデータ。呼び出し側が free する。失敗時 NULL
 */
static uint8_t *build_pgm_grayscale(const uint8_t *pixels, size_t pixel_len,
                                     uint16_t w, uint16_t h, size_t *out_len)
{
    // PGM ヘッダ: "P5\n{w} {h}\n255\n"
    char hdr[32];
    int hdr_len = snprintf(hdr, sizeof(hdr), "P5\n%u %u\n255\n", w, h);

    size_t expected_pixels = (size_t)w * h;
    size_t copy_len = (pixel_len < expected_pixels) ? pixel_len : expected_pixels;
    size_t file_size = (size_t)hdr_len + copy_len;

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) return NULL;

    memcpy(buf, hdr, hdr_len);
    memcpy(buf + hdr_len, pixels, copy_len);

    *out_len = file_size;
    return buf;
}

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
    uint16_t last_chunk_len;    // 最終チャンクのペイロード長
    cast_image_format_t format; // 画像フォーマット
    uint16_t width;             // 画像の幅(px)
    uint16_t height;            // 画像の高さ(px)
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
        ctx.width = header->width;
        ctx.height = header->height;
        ctx.chunks_received = 0;
        ctx.is_active = true;
    }

    // 2. オフセット計算と書き込み(ダイレクトマッピング)
    size_t offset = header->chunk_index * ctx.max_payload_ref;
    const uint8_t *payload = data + sizeof(cast_header_t);

    if (offset + header->payload_len <= ctx.allocated_size) {
        memcpy(ctx.buffer + offset, payload, header->payload_len);
        ctx.chunks_received++;

        // 最終チャンクの payload_len を記録（順不同対応）
        if (header->chunk_index == ctx.total_chunks - 1) {
            ctx.last_chunk_len = header->payload_len;
        }
    }

    if (ctx.chunks_received == ctx.total_chunks) {
        if (app_callback) {
            size_t final_size = (ctx.total_chunks - 1) * ctx.max_payload_ref + ctx.last_chunk_len;

            // フォーマットに応じてファイルヘッダを付与
            uint8_t *file_data = NULL;
            size_t file_len = 0;

            switch (ctx.format) {
            case CAST_FMT_RGB565:
                file_data = build_bmp_rgb565(ctx.buffer, final_size,
                                             ctx.width, ctx.height, &file_len);
                break;
            case CAST_FMT_GRAYSCALE:
                file_data = build_pgm_grayscale(ctx.buffer, final_size,
                                                 ctx.width, ctx.height, &file_len);
                break;
            default:
                // JPEG はそのまま
                break;
            }

            if (file_data) {
                app_callback(file_data, file_len, ctx.format, ctx.width, ctx.height);
                free(file_data);
            } else {
                // JPEG or ヘッダ生成失敗 → 生データをそのまま渡す
                app_callback(ctx.buffer, final_size, ctx.format, ctx.width, ctx.height);
            }
        }

        // callback から戻ったらミドルウェア側で解放
        // アプリはcallback内で必要ならコピーすること
        free(ctx.buffer);
        ctx.buffer = NULL;
        ctx.is_active = false;
    }
}
