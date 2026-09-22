# ROM転送モード

FC ROM Vomitterは3種類の経路でROMを受け取れます。どの経路も同じ制御APIを
使用し、iNES検査、非アクティブFlashスロットへの保存、CRC照合、SRAM照合、
ファミコン側バス分離を省略しません。

| モード | 初期状態 | 用途 |
|---|---|---|
| SoftAPブラウザ | 有効 | スマホから`http://192.168.4.1`へアップロード |
| USB-C直接転送 | 有効 | PCからWi-Fiなしで転送 |
| HTTPSクラウド取得 | 無効 | USB給電中の自動取得 |

## USB-C直接転送

```powershell
py -m pip install pyserial
py scripts/upload_rom_usb.py COM10 path\to\game.nes
```

USB Serial/JTAGへ長さとCRC32を含むフレームを送り、カートリッジは`RVOK`または
`RVER`で応答します。破損、不完全、サイズ超過、非iNESデータはFlashへ保存する
前に拒否されます。ファーム書き込み時はアップローダーを終了してください。

## HTTPSクラウド取得

公開リポジトリへ認証情報を入れないため、初期状態では無効です。

```powershell
pio run -d firmware -e esp32-s3 -t menuconfig
```

**ROM Vomitter FC**でクラウド取得を有効にし、Wi-Fi SSID、パスワード、生の
`.nes`を返すHTTPS URL、必要ならBearerトークンを設定します。HTTPS証明書は
ESP-IDFの証明書バンドルで検証します。最初のTLS通信前に`pool.ntp.org`から
時刻を同期し、証明書の有効期間も正しく確認します。SoftAPは復旧用として
同時に残ります。

初期設定では、ファミコン本体から電源が来ている間はクラウド更新を保留します。
USB単独給電時に取得・保存してから本体へ挿す運用が安全です。同一ROMは再保存
しないため、定期ポーリングでFlashスロットや書換回数を無駄にしません。

SSID、パスワード、トークンはローカルの`sdkconfig.*`と書込み済みバイナリに
含まれます。Gitへ追加せず、公開バイナリにも使用しないでください。
