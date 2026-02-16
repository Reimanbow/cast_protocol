/**
 * @file cast_internal.h
 */
#ifndef CAST_INTERNAL_H
#define CAST_INTERNAL_H

#include <stdint.h>
#include "cast_protocol.h"

#define CAST_MAGIC_BYTE 0xCA // CASTプロトコルの識別用

/**
 * @brief パケットタイプ(制御/データ)
 */
typedef enum {
    CAST_PKT_TYPE_DATA  = 0,    // 画像チャンク
    CAST_PKT_TYPE_ACK   = 1,    // 受信完了通知
    CAST_PKT_TYPE_NACK  = 2,    // 再送要求
    CAST_PKT_TYPE_CTRL  = 3     // 戦略変更命令
} cast_packet_type_t;

/**
 * @brief CASTプロトコル共通ヘッダ (13バイト)
 * * [フィールド再利用(Overloading)の設計指針]
 * プロトコルの軽量化のため、PacketTypeに応じて以下の通り意味を読み替える。
 * * 1. DATA時: すべてのフィールドを定義通り使用。
 * 2. ACK/NACK時:
 * - chunk_index: 「どのチャンク」に対する応答かを指定。
 * 特定の番号を入れることで Selective Repeat(選択的再送)を可能にする。
 * (例: NACKかつchunk_index=3なら「3番が欠落した」と解釈)
 * - total_chunks: 受信側が現在までに把握している累積のチャンク数などを格納可能。
 * 3. CTRL時:
 * - chunk_index / total_chunks: 命令の種類(Sub-Command ID)やパラメータとして再利用。
 * * ※ 無効なフィールドは原則として 0 または CAST_FMT_NONE で埋めること。
 */
typedef struct {
    uint8_t     magic;          // 0xCA（固定）
    uint8_t     type;           // cast_packet_type_t
    uint8_t     format;         // cast_image_format_t(DATA以外では無視)
    uint16_t    frame_id;       // 対象フレームID
    uint16_t    chunk_index;    // チャンク番号、または応答対象のチャンク番号
    uint16_t    total_chunks;   // 総チャンク数、または制御用パラメータ
    uint16_t    max_payload;    // 送信側が分割に使用した1チャンクの最大サイズ
    uint16_t    width;          // 画像の幅(px)。JPEGでは0可
    uint16_t    height;         // 画像の高さ(px)。JPEGでは0可
    uint16_t    payload_len;    // このパケットに含まれるデータ長(DATA以外では通常0)
    /* @note この後に最大MTU-2のサイズとなるまで実際のデータが入る */
} __attribute__((packed)) cast_header_t;

#define CAST_CRC_SIZE 2

// CRCを考慮したパケット最大オーバーヘッド
#define CAST_PROTOCOL_OVERHEAD (sizeof(cast_header_t) + CAST_CRC_SIZE)

#endif /* CAST_INTERNAL_H */