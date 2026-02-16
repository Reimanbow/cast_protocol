#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <fstream>
#include <cstring>

extern "C" {
#include "cast_protocol.h"
#include "cast_internal.h"
}

// ---------------------------------------------------------------------------
// Mock transport (Chunker送信キャプチャ + Reassemblerコールバック登録)
// ---------------------------------------------------------------------------

static std::vector<std::vector<uint8_t>> g_captured_packets;
static size_t g_mock_mtu = 64;
static void (*g_recv_callback)(const uint8_t *data, size_t len) = nullptr;

static esp_err_t mock_send(const uint8_t *data, size_t len)
{
    g_captured_packets.emplace_back(data, data + len);
    return ESP_OK;
}

static size_t mock_get_mtu(void) { return g_mock_mtu; }
static int16_t mock_get_rssi(void) { return -50; }
static bool mock_is_connected(void) { return true; }
static bool mock_is_ready(void) { return true; }

static esp_err_t mock_set_recv_cb(void (*cb)(const uint8_t *, size_t))
{
    g_recv_callback = cb;
    return ESP_OK;
}

static cast_transport_interface_t g_mock_transport = {
    .send              = mock_send,
    .get_mtu           = mock_get_mtu,
    .get_rssi          = mock_get_rssi,
    .is_connected      = mock_is_connected,
    .is_ready          = mock_is_ready,
    .set_recv_callback = mock_set_recv_cb,
};

// ---------------------------------------------------------------------------
// 完成フレームのキャプチャ
// ---------------------------------------------------------------------------

struct CapturedFrame {
    std::vector<uint8_t> data;
    cast_image_format_t fmt;
    uint16_t width;
    uint16_t height;
};

static std::vector<CapturedFrame> g_completed_frames;

static void on_frame_ready(const uint8_t *data, size_t len, cast_image_format_t fmt, uint16_t width, uint16_t height)
{
    CapturedFrame f;
    f.data.assign(data, data + len);
    f.fmt = fmt;
    f.width = width;
    f.height = height;
    g_completed_frames.push_back(std::move(f));
}

// ---------------------------------------------------------------------------
// ヘルパー: キャプチャしたパケットをReassemblerに投入
// ---------------------------------------------------------------------------

static void feed_all_packets()
{
    for (auto &pkt : g_captured_packets) {
        g_recv_callback(pkt.data(), pkt.size());
    }
}

static void feed_packets_in_order(const std::vector<size_t> &order)
{
    for (size_t idx : order) {
        g_recv_callback(g_captured_packets[idx].data(), g_captured_packets[idx].size());
    }
}

// ヘルパー: ファイルをバイト列として読み込む
static std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream ifs(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(ifs),
        std::istreambuf_iterator<char>()
    );
}

// ---------------------------------------------------------------------------
// テストフィクスチャ
// ---------------------------------------------------------------------------

class ReassemblerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_captured_packets.clear();
        g_completed_frames.clear();
        g_mock_mtu = 64;
        g_recv_callback = nullptr;
        cast_init_receiver(&g_mock_transport, on_frame_ready);
    }

    void TearDown() override
    {
        cast_reassembler_reset();
    }
};

// ===========================================================================
// 1. 正常系：一気通貫（Loopback）テスト
// ===========================================================================

// 1-1: 最小データ (1バイト)
TEST_F(ReassemblerTest, LoopbackMinimal)
{
    uint8_t data[] = {0x42};
    cast_send_frame(data, 1, CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 1u);

    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data[0], 0x42);
}

// 1-2: MTUぴったり (max_payload と同じデータ長)
TEST_F(ReassemblerTest, LoopbackExactMtu)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload);
    std::iota(data.begin(), data.end(), 0);

    // JPEG で送信（生データがそのまま返る）
    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 1u);

    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data, data);
}

// 1-3: MTU+1バイト (2チャンク、端数1バイト)
TEST_F(ReassemblerTest, LoopbackMtuPlusOne)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload + 1);
    std::iota(data.begin(), data.end(), 0);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 2u);

    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data, data);
}

// 1-4: 実JPEG画像 (16x16)
TEST_F(ReassemblerTest, LoopbackJpeg16x16)
{
    auto jpeg = read_file(std::string(TEST_DATA_DIR) + "/placeholder_jp_16x16.jpg");
    ASSERT_FALSE(jpeg.empty());

    cast_send_frame(jpeg.data(), jpeg.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), jpeg.size());
    EXPECT_EQ(g_completed_frames[0].data, jpeg);
    EXPECT_EQ(g_completed_frames[0].fmt, CAST_FMT_JPEG);
}

// 1-4b: 実JPEG画像 (200x200)
TEST_F(ReassemblerTest, LoopbackJpeg200x200)
{
    auto jpeg = read_file(std::string(TEST_DATA_DIR) + "/placeholder_jp_200x200.jpg");
    ASSERT_FALSE(jpeg.empty());

    cast_send_frame(jpeg.data(), jpeg.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), jpeg.size());
    EXPECT_EQ(g_completed_frames[0].data, jpeg);
}

// 1-4c: 実JPEG画像 (640x480) - ESP-NOW相当のMTU
TEST_F(ReassemblerTest, LoopbackJpeg640x480_LargeMtu)
{
    g_mock_mtu = 250;
    // MTU変更後にreceiverを再初期化
    cast_init_receiver(&g_mock_transport, on_frame_ready);

    auto jpeg = read_file(std::string(TEST_DATA_DIR) + "/placeholder_jp_640x480.jpg");
    ASSERT_FALSE(jpeg.empty());

    cast_send_frame(jpeg.data(), jpeg.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), jpeg.size());
    EXPECT_EQ(g_completed_frames[0].data, jpeg);
}

// ===========================================================================
// 2. 無線耐性系：順序制御テスト
// ===========================================================================

// 2-1: 逆順到着
TEST_F(ReassemblerTest, ReverseOrder)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload * 2 + 10);
    std::iota(data.begin(), data.end(), 0);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 3u);

    // 逆順: [2] → [1] → [0]
    feed_packets_in_order({2, 1, 0});

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data, data);
}

// 2-2: ランダム順序 (JPEG 200x200を使用)
TEST_F(ReassemblerTest, RandomOrder)
{
    auto jpeg = read_file(std::string(TEST_DATA_DIR) + "/placeholder_jp_200x200.jpg");
    ASSERT_FALSE(jpeg.empty());

    cast_send_frame(jpeg.data(), jpeg.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    size_t n = g_captured_packets.size();
    ASSERT_GT(n, 2u);

    // インデックスをシャッフル
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(12345); // 固定シード（再現性のため）
    std::shuffle(order.begin(), order.end(), rng);

    feed_packets_in_order(order);

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), jpeg.size());
    EXPECT_EQ(g_completed_frames[0].data, jpeg);
}

// ===========================================================================
// 3. 異常系：フレーム管理・リセットテスト
// ===========================================================================

// 3-1: フレームID切り替わりによるリセット
TEST_F(ReassemblerTest, FrameIdSwitchResetsState)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data1(max_payload * 2 + 5);
    std::iota(data1.begin(), data1.end(), 0);

    std::vector<uint8_t> data2(max_payload + 10);
    std::iota(data2.begin(), data2.end(), 100);

    // フレーム1を送信（3チャンク）
    cast_send_frame(data1.data(), data1.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    size_t frame1_count = g_captured_packets.size();

    // フレーム1のチャンク0だけ投入（未完成）
    g_recv_callback(g_captured_packets[0].data(), g_captured_packets[0].size());
    EXPECT_EQ(g_completed_frames.size(), 0u);

    // フレーム2を送信
    g_captured_packets.clear();
    cast_send_frame(data2.data(), data2.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);

    // フレーム2の全チャンクを投入 → フレーム1がリセットされフレーム2が完成
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data, data2);
}

// 3-2: 短すぎるパケットは無視される
TEST_F(ReassemblerTest, TooShortPacketIgnored)
{
    uint8_t short_data[] = {0xCA, 0x00};
    g_recv_callback(short_data, sizeof(short_data));

    EXPECT_EQ(g_completed_frames.size(), 0u);
}

// 3-3: magicバイトが違うパケットは無視される
TEST_F(ReassemblerTest, WrongMagicIgnored)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(10, 0xAA);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 1u);

    // magicバイトを壊す
    g_captured_packets[0][0] = 0xFF;
    feed_all_packets();

    EXPECT_EQ(g_completed_frames.size(), 0u);
}

// 3-4: バッファオーバーランチェック
// chunk_index が total_chunks を超える不正なパケットを投入
TEST_F(ReassemblerTest, OutOfBoundsChunkIndexIgnored)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload * 2 + 1);
    std::iota(data.begin(), data.end(), 0);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 3u);

    // チャンク0を投入（フレーム初期化）
    g_recv_callback(g_captured_packets[0].data(), g_captured_packets[0].size());

    // 不正パケット: chunk_index を 999 に改ざん
    auto bad_pkt = g_captured_packets[1];
    cast_header_t *h = (cast_header_t *)bad_pkt.data();
    h->chunk_index = 999;
    g_recv_callback(bad_pkt.data(), bad_pkt.size());

    // 正常なチャンク1, 2を投入して完成させる
    g_recv_callback(g_captured_packets[1].data(), g_captured_packets[1].size());
    g_recv_callback(g_captured_packets[2].data(), g_captured_packets[2].size());

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data, data);
}

// ===========================================================================
// 4. アプリ連携系：フォーマット・完了通知テスト
// ===========================================================================

// 4-1: フォーマット情報の伝搬（ヘッダ付与を含む）
TEST_F(ReassemblerTest, FormatPropagation)
{
    std::vector<uint8_t> data(50, 0x11);

    // JPEG: 生データがそのまま返る
    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    feed_all_packets();
    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].fmt, CAST_FMT_JPEG);
    EXPECT_EQ(g_completed_frames[0].width, 0);
    EXPECT_EQ(g_completed_frames[0].height, 0);
    EXPECT_EQ(g_completed_frames[0].data, data); // そのまま

    // RGB565: BMP ヘッダ付き
    g_captured_packets.clear();
    g_completed_frames.clear();
    cast_send_frame(data.data(), data.size(), CAST_FMT_RGB565, 320, 240, &g_mock_transport);
    feed_all_packets();
    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].fmt, CAST_FMT_RGB565);
    EXPECT_EQ(g_completed_frames[0].width, 320);
    EXPECT_EQ(g_completed_frames[0].height, 240);
    // BMP signature "BM"
    ASSERT_GE(g_completed_frames[0].data.size(), 2u);
    EXPECT_EQ(g_completed_frames[0].data[0], 'B');
    EXPECT_EQ(g_completed_frames[0].data[1], 'M');

    // GRAYSCALE: PGM ヘッダ付き
    g_captured_packets.clear();
    g_completed_frames.clear();
    cast_send_frame(data.data(), data.size(), CAST_FMT_GRAYSCALE, 160, 120, &g_mock_transport);
    feed_all_packets();
    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].fmt, CAST_FMT_GRAYSCALE);
    EXPECT_EQ(g_completed_frames[0].width, 160);
    EXPECT_EQ(g_completed_frames[0].height, 120);
    // PGM signature "P5\n"
    ASSERT_GE(g_completed_frames[0].data.size(), 3u);
    EXPECT_EQ(g_completed_frames[0].data[0], 'P');
    EXPECT_EQ(g_completed_frames[0].data[1], '5');
    EXPECT_EQ(g_completed_frames[0].data[2], '\n');
}

// 4-1b: RGB565 → BMP 変換の検証
TEST_F(ReassemblerTest, BmpHeaderRgb565)
{
    // 4x2 の RGB565 画像 = 16 bytes
    const uint16_t w = 4, h = 2;
    std::vector<uint8_t> pixels(w * h * 2);
    std::iota(pixels.begin(), pixels.end(), 0);

    cast_send_frame(pixels.data(), pixels.size(), CAST_FMT_RGB565, w, h, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    auto &frame = g_completed_frames[0];

    // BMP ヘッダサイズ = 66 bytes
    const size_t BMP_HDR = 66;
    ASSERT_GE(frame.data.size(), BMP_HDR);

    // BMP signature
    EXPECT_EQ(frame.data[0], 'B');
    EXPECT_EQ(frame.data[1], 'M');

    // row_stride: ((4*2+3)/4)*4 = 8 (パディングなし)
    uint32_t row_stride = ((w * 2 + 3) / 4) * 4;
    uint32_t expected_file_size = BMP_HDR + row_stride * h;
    EXPECT_EQ(frame.data.size(), expected_file_size);

    // ファイルサイズフィールド (offset 2, 4 bytes LE)
    uint32_t file_size_field;
    memcpy(&file_size_field, &frame.data[2], 4);
    EXPECT_EQ(file_size_field, expected_file_size);

    // データオフセット (offset 10, 4 bytes LE)
    uint32_t data_offset;
    memcpy(&data_offset, &frame.data[10], 4);
    EXPECT_EQ(data_offset, (uint32_t)BMP_HDR);

    // bpp (offset 28, 2 bytes LE)
    uint16_t bpp;
    memcpy(&bpp, &frame.data[28], 2);
    EXPECT_EQ(bpp, 16);

    // ピクセルデータが BMP_HDR 以降に含まれている
    // 行0のピクセルデータを確認
    for (size_t i = 0; i < w * 2; i++) {
        EXPECT_EQ(frame.data[BMP_HDR + i], pixels[i])
            << "Mismatch at pixel byte " << i;
    }
}

// 4-1c: Grayscale → PGM 変換の検証
TEST_F(ReassemblerTest, PgmHeaderGrayscale)
{
    // 4x2 の Grayscale 画像 = 8 bytes
    const uint16_t w = 4, h = 2;
    std::vector<uint8_t> pixels(w * h);
    std::iota(pixels.begin(), pixels.end(), 100);

    cast_send_frame(pixels.data(), pixels.size(), CAST_FMT_GRAYSCALE, w, h, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    auto &frame = g_completed_frames[0];

    // PGM ヘッダ: "P5\n4 2\n255\n" = 12 bytes
    std::string expected_hdr = "P5\n4 2\n255\n";
    ASSERT_GE(frame.data.size(), expected_hdr.size() + pixels.size());

    // ヘッダ部分の一致
    std::string actual_hdr(frame.data.begin(), frame.data.begin() + expected_hdr.size());
    EXPECT_EQ(actual_hdr, expected_hdr);

    // ピクセルデータの一致
    size_t offset = expected_hdr.size();
    for (size_t i = 0; i < pixels.size(); i++) {
        EXPECT_EQ(frame.data[offset + i], pixels[i])
            << "Mismatch at pixel byte " << i;
    }

    // 全体サイズ
    EXPECT_EQ(frame.data.size(), expected_hdr.size() + pixels.size());
}

// 4-2: 最終サイズの完全一致（連番データ）
TEST_F(ReassemblerTest, FinalSizeExact)
{
    // 端数が出るサイズで確認
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload * 3 + 7);
    std::iota(data.begin(), data.end(), 0);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    feed_all_packets();

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), data.size());
    EXPECT_EQ(g_completed_frames[0].data, data);
}

// 4-2b: 最終サイズの完全一致（逆順でも正しいサイズ）
TEST_F(ReassemblerTest, FinalSizeExactReverseOrder)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(max_payload * 3 + 7);
    std::iota(data.begin(), data.end(), 0);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, 0, 0, &g_mock_transport);
    size_t n = g_captured_packets.size();

    // 逆順投入
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::reverse(order.begin(), order.end());
    feed_packets_in_order(order);

    ASSERT_EQ(g_completed_frames.size(), 1u);
    EXPECT_EQ(g_completed_frames[0].data.size(), data.size());
    EXPECT_EQ(g_completed_frames[0].data, data);
}
