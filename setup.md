# 導入方法

Qua Station（RTD1295）用の USB レスキュー U-Boot です。
**導入には AI アシスタントの活用をおすすめします。** このリポジトリと本手順を渡し、対象機の構成確認と個体別バンドルの準備を依頼してください。

## 1. 準備

Release の bin とソースをダウンロードします。
この bin は指定のファームウェア構成に対応しており、対象個体用の `plan.json`、`manifest.json`、`trial-slot.bin` とインストーラーをまとめたバンドルが別途必要です。
公開 bin 単体ではインストールできません。eMMC 全体や純正 U-Boot 領域へ直接書き込まないでください。

## 2. インストール

対象機の Linux 上で、用意したバンドルのディレクトリから実行します。Python 3 と管理者権限が必要です。
初回は TTL で起動ログを取得してください。

```sh
sudo python3 install-usb-chain.py . stage
sudo python3 install-usb-chain.py . arm
```

成功したら再起動します。作成されたバックアップは保管してください。
構成が一致しない場合は停止するので、AI に出力を渡して原因を確認してください。

## 3. USB で接続

独立した USB 2.0 Type-A ポートを PC に接続し、起動時にコンソールを開きます。
macOS での例:

```sh
screen /dev/cu.usbmodemQuaRescue1 115200
```

- `version`：バージョン表示
- `boot`：Linux を起動
- 操作なし：純正 U-Boot の2秒＋レスキューの3秒の受付後、Linux へ進む

## 元に戻す

同じバンドルを使い、対象機の Linux 上で実行します。

```sh
sudo python3 install-usb-chain.py . restore
```

純正の通常起動設定に戻します。保存済みの純正ブートローダーバイナリは変更しません。
