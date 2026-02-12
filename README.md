# Cast Protocol

ESP-IDF component for cast protocol (ESP32-S3).

## Requirements

- ESP-IDF v5.0 or later

## Installation

Add the dependency to your project's `idf_component.yml`:

```yaml
dependencies:
  cast_protocol:
    version: "*"
```

Then run:

```bash
idf.py reconfigure
```

## Usage

```c
#include "cast_protocol.h"

void app_main(void)
{
    ESP_ERROR_CHECK(cast_protocol_init());

    // Your application code here

    ESP_ERROR_CHECK(cast_protocol_deinit());
}
```

## Component Development Guide

### バージョニング

`idf_component.yml` の `version` はセマンティックバージョニング (`major.minor.patch`) に従います。
レジストリに公開されたバージョンは**上書き不可**なので、変更するたびにバージョンを上げる必要があります。

依存側で使えるバージョン制約:

| 構文 | 意味 | 例 |
|---|---|---|
| `*` | 任意のバージョン | `*` |
| `>=`, `<` 等 | 比較演算 | `>=1.0.0` |
| `^` (caret) | 最左の非ゼロ桁を固定 | `^1.2.3` = `>=1.2.3,<2.0.0` |
| `~` (tilde) | パッチレベルのみ変動 | `~1.2.3` = `>=1.2.3,<1.3.0` |

注意: `^0.x.y` は `>=0.0.0,<1.0.0` ではなく、マイナーバージョンで固定されます (`^0.2.3` = `>=0.2.3,<0.3.0`)。

### 公開に必要なファイル

レジストリへの公開には最低限以下が必要です:

- `idf_component.yml` (`version` フィールド必須)
- `LICENSE` または `LICENSE.txt`
- `README.md`

### idf_component.yml の主なフィールド

```yaml
version: "1.0.0"                    # 必須
description: "My component"
license: "MIT"                      # SPDX 識別子
url: "https://github.com/..."
repository: "https://github.com/....git"
targets:                            # 省略すると全ターゲット対応
  - esp32s3
dependencies:
  idf:
    version: ">=5.0.0"
  espressif/button:                 # namespace/name 形式
    version: "^3.0.0"
```

- `path:` や `git:` による依存は**公開時に使用不可**（ローカル開発専用）
- `targets` を省略すると全チップ対応として扱われます

### namespace

- レジストリ上のコンポーネントは `namespace/component_name` で識別されます
- namespace を省略すると `espressif` がデフォルトになります
- GitHub ログイン時にユーザー名と同名の namespace が自動作成されます

### examples ディレクトリ

- `examples/` 配下の各プロジェクトはレジストリ上で個別にダウンロード可能になります
- 各 example は**自己完結**している必要があります（example 外のファイルに依存しない）
- example の `idf_component.yml` では `override_path` でローカル開発時に親コンポーネントを参照します:

```yaml
dependencies:
  my_namespace/my_component:
    version: "*"
    override_path: "../../"
```

- example の `CMakeLists.txt` で `EXTRA_COMPONENT_DIRS` を使ってはいけません（レジストリからダウンロードした場合に壊れます）

### 公開方法

```bash
# パッケージの確認（アップロードせずアーカイブ作成）
compote component pack --name cast_protocol

# アップロード
compote component upload --name cast_protocol --namespace my_namespace
```

CI/CD では環境変数 `IDF_COMPONENT_API_TOKEN` を設定するか、GitHub Actions の OIDC を利用できます。

## License

MIT License
