#include <gtest/gtest.h>
#include <vector>
#include <cstring>

extern "C" {
#include "cast_protocol.h"
#include "cast_internal.h"
}

// ---------------------------------------------------------------------------
// Mock transport
// ---------------------------------------------------------------------------

// キャプチャしたパケットを保持するグローバル変数
static std::vector<std::vector<uint8_t>> g_captured_packets;
static size_t g_mock_mtu = 64;
static esp_err_t g_send_return = ESP_OK;
static int g_send_fail_at = -1; // N番目のsendで失敗させる (-1=失敗しない)

static esp_err_t mock_send(const uint8_t *data, size_t len)
{
    if (g_send_fail_at >= 0 &&
        (int)g_captured_packets.size() == g_send_fail_at) {
        return ESP_FAIL;
    }
    g_captured_packets.emplace_back(data, data + len);
    return g_send_return;
}

static size_t mock_get_mtu(void) { return g_mock_mtu; }
static int16_t mock_get_rssi(void) { return -50; }
static bool mock_is_connected(void) { return true; }
static bool mock_is_ready(void) { return true; }
static esp_err_t mock_set_recv_cb(void (*)(const uint8_t *, size_t)) { return ESP_OK; }

static cast_transport_interface_t g_mock_transport = {
    .send              = mock_send,
    .get_mtu           = mock_get_mtu,
    .get_rssi          = mock_get_rssi,
    .is_connected      = mock_is_connected,
    .is_ready          = mock_is_ready,
    .set_recv_callback = mock_set_recv_cb,
};

// ---------------------------------------------------------------------------
// ヘルパー
// ---------------------------------------------------------------------------

static const cast_header_t *packet_header(size_t index)
{
    return reinterpret_cast<const cast_header_t *>(g_captured_packets[index].data());
}

static const uint8_t *packet_payload(size_t index)
{
    return g_captured_packets[index].data() + sizeof(cast_header_t);
}

// ---------------------------------------------------------------------------
// テストフィクスチャ
// ---------------------------------------------------------------------------

class ChunkerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_captured_packets.clear();
        g_mock_mtu      = 64;
        g_send_return   = ESP_OK;
        g_send_fail_at  = -1;
    }
};

// ===========================================================================
// ① チャンク分割数の計算テスト
// ===========================================================================

// データが max_payload の倍数 → 端数なし
TEST_F(ChunkerTest, ExactFitChunkCount)
{
    // MTU=64, overhead=sizeof(cast_header_t)+2=13, max_payload=51
    // データ102バイト → 102/51 = ちょうど2チャンク
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    const size_t data_len = max_payload * 2;
    std::vector<uint8_t> data(data_len, 0xAA);

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(g_captured_packets.size(), 2u);
}

// データが max_payload の倍数でない → 端数あり
TEST_F(ChunkerTest, PartialLastChunkCount)
{
    // max_payload=51, データ100バイト → ceil(100/51) = 2チャンク
    std::vector<uint8_t> data(100, 0xBB);

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(g_captured_packets.size(), 2u);
}

// データが max_payload 未満 → 1チャンク
TEST_F(ChunkerTest, SingleChunk)
{
    std::vector<uint8_t> data(10, 0xCC);

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(g_captured_packets.size(), 1u);
}

// 3チャンクに分割されるケース
TEST_F(ChunkerTest, ThreeChunks)
{
    // max_payload=51, データ130バイト → ceil(130/51) = 3チャンク (51+51+28)
    std::vector<uint8_t> data(130, 0xDD);

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(g_captured_packets.size(), 3u);
}

// ===========================================================================
// ② ヘッダの一貫性テスト
// ===========================================================================

TEST_F(ChunkerTest, HeaderConsistency)
{
    std::vector<uint8_t> data(130, 0x00);

    cast_send_frame(data.data(), data.size(), CAST_FMT_RGB565, &g_mock_transport);

    ASSERT_EQ(g_captured_packets.size(), 3u);

    const auto *h0 = packet_header(0);
    const auto *h1 = packet_header(1);
    const auto *h2 = packet_header(2);

    // magic は全パケットで 0xCA
    EXPECT_EQ(h0->magic, CAST_MAGIC_BYTE);
    EXPECT_EQ(h1->magic, CAST_MAGIC_BYTE);
    EXPECT_EQ(h2->magic, CAST_MAGIC_BYTE);

    // type は全パケットで DATA
    EXPECT_EQ(h0->type, CAST_PKT_TYPE_DATA);
    EXPECT_EQ(h1->type, CAST_PKT_TYPE_DATA);
    EXPECT_EQ(h2->type, CAST_PKT_TYPE_DATA);

    // format は渡した値と一致
    EXPECT_EQ(h0->format, CAST_FMT_RGB565);
    EXPECT_EQ(h1->format, CAST_FMT_RGB565);
    EXPECT_EQ(h2->format, CAST_FMT_RGB565);

    // frame_id は同一フレーム内で全て同じ
    EXPECT_EQ(h0->frame_id, h1->frame_id);
    EXPECT_EQ(h1->frame_id, h2->frame_id);

    // total_chunks は全パケットで同じ値(3)
    EXPECT_EQ(h0->total_chunks, 3);
    EXPECT_EQ(h1->total_chunks, 3);
    EXPECT_EQ(h2->total_chunks, 3);

    // chunk_index が 0, 1, 2 と連番
    EXPECT_EQ(h0->chunk_index, 0);
    EXPECT_EQ(h1->chunk_index, 1);
    EXPECT_EQ(h2->chunk_index, 2);
}

// ===========================================================================
// ③ ペイロード境界テスト
// ===========================================================================

TEST_F(ChunkerTest, PayloadBoundary)
{
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;

    // 0, 1, 2, ... , 129 の連番データ
    std::vector<uint8_t> data(130);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    ASSERT_EQ(g_captured_packets.size(), 3u);

    // 各チャンクの payload_len を確認
    const size_t expect_len0 = max_payload;              // 51
    const size_t expect_len1 = max_payload;              // 51
    const size_t expect_len2 = 130 - max_payload * 2;    // 28

    EXPECT_EQ(packet_header(0)->payload_len, expect_len0);
    EXPECT_EQ(packet_header(1)->payload_len, expect_len1);
    EXPECT_EQ(packet_header(2)->payload_len, expect_len2);

    // チャンク0の末尾とチャンク1の先頭が連続しているか
    const uint8_t *p0 = packet_payload(0);
    const uint8_t *p1 = packet_payload(1);
    EXPECT_EQ(p0[0], 0);                          // 先頭
    EXPECT_EQ(p0[max_payload - 1], max_payload - 1); // チャンク0の末尾
    EXPECT_EQ(p1[0], max_payload);                 // チャンク1の先頭 = チャンク0の続き

    // チャンク2（最後）の先頭と末尾
    const uint8_t *p2 = packet_payload(2);
    EXPECT_EQ(p2[0], (uint8_t)(max_payload * 2));
    EXPECT_EQ(p2[expect_len2 - 1], 129);

    // 全ペイロードを結合して元データと一致するか
    std::vector<uint8_t> reassembled;
    for (size_t i = 0; i < g_captured_packets.size(); i++) {
        const uint8_t *payload = packet_payload(i);
        uint16_t plen = packet_header(i)->payload_len;
        reassembled.insert(reassembled.end(), payload, payload + plen);
    }
    EXPECT_EQ(reassembled.size(), data.size());
    EXPECT_EQ(reassembled, data);
}

// ===========================================================================
// ④ frame_id インクリメントテスト
// ===========================================================================

TEST_F(ChunkerTest, FrameIdIncrements)
{
    std::vector<uint8_t> data(10, 0xFF);

    // 1回目
    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 1u);
    uint16_t frame_id_1 = packet_header(0)->frame_id;

    // 2回目
    g_captured_packets.clear();
    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);
    ASSERT_EQ(g_captured_packets.size(), 1u);
    uint16_t frame_id_2 = packet_header(0)->frame_id;

    EXPECT_EQ(frame_id_2, frame_id_1 + 1);
}

// ===========================================================================
// ⑤ 送信失敗テスト
// ===========================================================================

// 2番目のsendで失敗 → 即座にエラーを返し、残りは送らない
TEST_F(ChunkerTest, SendFailureMidway)
{
    std::vector<uint8_t> data(130, 0xEE); // 3チャンク
    g_send_fail_at = 1; // 2番目(index=1)のsendで失敗

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_FAIL);
    // 1番目のパケットだけキャプチャされている(2番目で失敗したのでpushされない)
    EXPECT_EQ(g_captured_packets.size(), 1u);
}

// 最初のsendで失敗
TEST_F(ChunkerTest, SendFailureOnFirst)
{
    std::vector<uint8_t> data(10, 0x00);
    g_send_fail_at = 0;

    esp_err_t ret = cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    EXPECT_EQ(ret, ESP_FAIL);
    EXPECT_EQ(g_captured_packets.size(), 0u);
}

// ===========================================================================
// ⑥ パケットサイズがMTUを超えないことの確認
// ===========================================================================

TEST_F(ChunkerTest, PacketSizeNeverExceedsMtu)
{
    std::vector<uint8_t> data(500, 0x42);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    for (size_t i = 0; i < g_captured_packets.size(); i++) {
        EXPECT_LE(g_captured_packets[i].size(), g_mock_mtu)
            << "Packet " << i << " exceeds MTU";
    }
}

// ===========================================================================
// ⑦ 異なるMTUでの動作確認
// ===========================================================================

TEST_F(ChunkerTest, SmallMtu)
{
    g_mock_mtu = 24; // overhead=13 → max_payload=11
    const size_t max_payload = g_mock_mtu - CAST_PROTOCOL_OVERHEAD;
    std::vector<uint8_t> data(50, 0x11);

    cast_send_frame(data.data(), data.size(), CAST_FMT_GRAYSCALE, &g_mock_transport);

    size_t expected_chunks = (50 + max_payload - 1) / max_payload;
    EXPECT_EQ(g_captured_packets.size(), expected_chunks);

    for (size_t i = 0; i < g_captured_packets.size(); i++) {
        EXPECT_LE(g_captured_packets[i].size(), g_mock_mtu);
    }
}

TEST_F(ChunkerTest, LargeMtu)
{
    g_mock_mtu = 250; // ESP-NOW相当
    std::vector<uint8_t> data(200, 0x22);

    cast_send_frame(data.data(), data.size(), CAST_FMT_JPEG, &g_mock_transport);

    // max_payload=237, 200 < 237 → 1チャンク
    EXPECT_EQ(g_captured_packets.size(), 1u);
    EXPECT_EQ(packet_header(0)->payload_len, 200);
}
