# Qua rescue U-Boot

KDDI Qua Station（RTD1295）向けの RAM 常駐型レスキュー U-Boot です。
純正 U-Boot から起動し、USB 2.0 CDC ACM コンソールを提供した後、純正 AArch64 U-Boot 経由で Linux を起動します。
保存済みの純正 ARM32 / ARM64 ブートローダーバイナリは置き換えません。

```text
純正 ARM32 U-Boot（2秒）→ RAM レスキュー U-Boot（3秒）→ 純正 ARM64 U-Boot → Linux
                              └ USB / TTL コンソール
```

インストールと操作方法は [setup.md](setup.md) を参照してください。
配布バイナリは GitHub Releases に掲載します。

## 実機で確認した動作

- USB コンソールで `version` と `boot` を実行し、Linux まで起動。
- コンソールを開かずに待機すると、3秒後に Linux の起動へ進む。
- 両方の純正環境バンクに設定を保存し、読み戻しで確認。
- 保存済みの純正 ARM32 / ARM64 ブートローダーのハッシュが変更されていないことを確認。

2秒＋3秒は操作受付時間です。電源投入から Linux 起動までの所要時間ではありません。
WPS によるバイパス、破損イメージの注入、ハング時の watchdog 復旧は実機未確認です。

## ソース

上流: [Haruroid/u-boot-kts31](https://github.com/Haruroid/u-boot-kts31)

ベースコミット: `7145bdb850c3527af9bff67be7a98f0e7daacbff`

- `common/qua_usb_console.c`: USB CDC ACM コンソール
- `common/qua_rescue_window.c`: 3秒の操作受付
- `common/qua_chain_linux.c`: CRC 検証と純正ブートローダーへの引き継ぎ
- `scripts/`: パッケージ作成、インストール、復元用ツール
- `profiles/`: プロファイルの記入例

ライセンスは [COPYING](COPYING) を参照してください。
