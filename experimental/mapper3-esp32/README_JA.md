# ESP32-S3による2バンクMapper 3実験

[English](README.md) · [改造配線](HARDWARE-PATCH.md) · [検証記録](validation.json)

これはFC ROM Vomitter Rev A-FC実機に対する、製造後の実験です。搭載済み
ESP32-S3でMapper 3のCPU書き込みを観測し、CHR SRAMのA13を切り替えます。
追加MCUや外付けラッチなしで、8 KiBのCHRを2バンク扱うことを狙っています。

> [!CAUTION]
> これはジャンパ線とピン浮かしを伴う実験で、改版済み製造データでは
> ありません。安定版の`hardware/`と製造リリースは変更していません。

## 対応範囲

- Mapper 0: PRG 16/32 KiB、CHR 8 KiB
- Mapper 3: PRG 16/32 KiB、CHR 8 KiB × 2、CPU D0でバンク選択
- 非対応: CHR-RAM、4バンクCNROM、UNROM、MMC1/MMC3、拡張音源

GPIO36がU14のA13を駆動し、GPIO42/43/44でCPU D0、`ROMSEL_inv`、
`PRG_EN_n`を観測します。具体的な半田付け箇所と安全確認は
[HARDWARE-PATCH.md](HARDWARE-PATCH.md)を参照してください。

## 現在の評価

2026-08-07時点で、ホストテスト、ESP32-S3ビルド、IRAM上の直接GPIO処理は
PASSです。改造した1枚ではMapper 0の退行がなく、2バンクMapper 3の
自作ROMも最終版ではエミュレータに近い挙動になりました。

ただし、入力電圧余裕、最悪条件のタイミング、長時間Wi-Fi負荷、複数基板での
再現性は未認定です。したがって状態は**限定的な実機実験 / USER_REVIEW**で、
Rev A-FC安定版が正式にMapper 3対応になったわけではありません。

## ビルド

```powershell
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build --config Release
ctest --test-dir host_tests/build -C Release --output-on-failure
pio run -e esp32-s3 -j 1
```

`/api/status`でバンクと書き込み回数を確認できます。BOOTボタンで観測記録を
NVSへ保存し、再起動後に`/api/mapper-trace`から取得できます。
