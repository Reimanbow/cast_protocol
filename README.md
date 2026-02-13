# CAST Protocol

**CAST (Camera Adaptive Strategy Transmission)** は、ESP32-S3 向けの画像伝送ミドルウェアです。
カメラで撮影した画像フレームを MTU に合わせてチャンク分割・送信し、受信側で順不同のチャンクを組み立てて元のフレームを復元します。

トランスポート層（ESP-NOW, Wi-Fi など）は抽象化されており、`cast_transport_interface_t` を実装するだけで任意の通信方式に対応できます。

## Features

- 画像フレームの MTU ベース自動分割送信 (Chunker)
- 順不同チャンクの受信・組み立て (Reassembler)
- JPEG / RGB565 / Grayscale フォーマット対応
- トランスポート層の抽象化 (`cast_transport_interface_t`)
- バッファ管理はミドルウェア側で完結（コールバックで通知）

## Requirements

- ESP-IDF v5.4 or later
- Target: ESP32-S3

## Packet Structure

```
+-------+----------+---------+
| Header (15B) | Payload | CRC (2B) |
+-------+----------+---------+
```

ヘッダには magic byte, パケットタイプ, フォーマット, フレームID, チャンク番号, 総チャンク数, 最大ペイロード長, ペイロード長が含まれます。

## Usage

### Sender (Chunker)

```c
#include "cast_protocol.h"

// トランスポートインタフェースを実装
cast_transport_interface_t transport = {
    .send = my_send,
    .get_mtu = my_get_mtu,
    .get_rssi = my_get_rssi,
    .is_connected = my_is_connected,
    .is_ready = my_is_ready,
    .set_recv_callback = my_set_recv_callback,
};

// フレーム送信
esp_err_t err = cast_send_frame(jpeg_buf, jpeg_len, CAST_FMT_JPEG, &transport);
```

### Receiver (Reassembler)

```c
#include "cast_protocol.h"

// フレーム完成時のコールバック
// data はコールバックから戻った後にミドルウェアが解放する
// 保持する場合はコールバック内でコピーすること
void on_frame_ready(const uint8_t *data, size_t len, cast_image_format_t fmt) {
    // data を表示・保存など
}

// 受信側の初期化
esp_err_t err = cast_init_receiver(&transport, on_frame_ready);
```

## API

| Function | Description |
|---|---|
| `cast_send_frame()` | 画像フレームをチャンク分割して送信 |
| `cast_init_receiver()` | 受信側を初期化しコールバックを登録 |
| `cast_reassembler_reset()` | 受信中のフレーム組み立てをリセット |

## License

MIT License
