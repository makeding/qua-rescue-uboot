# セットアップ

## 対象と配布ファイル

対象は KDDI Qua Station（RTD1295）です。GitHub Releases から、使用するファームウェアのレイアウトと一致する配布ファイルをダウンロードしてください。

現在のレスキューイメージには、カーネル、DTB、純正 U-Boot、環境領域などの CRC が組み込まれています。
`candidate.bin` 単体は汎用の eMMC イメージではありません。そのまま eMMC 全体や純正ブートローダー領域へ書き込まないでください。
インストールには、対象個体に対応する以下のバンドルが必要です。

```text
plan.json                    対象個体の CID、配置、ハッシュ、通常起動設定
manifest.json                配布イメージの情報とハッシュ
trial-slot.bin               割り当て済み領域に書き込むペイロード
install-usb-chain.py          インストーラー
install_helpers.py           環境領域の読み書き処理
```

`plan.json`、元の環境領域、バックアップを含む個体別バンドルは公開 Release に含めないでください。
対応するバンドルがない場合、公開 bin だけではこの手順を実行できません。

## インストール

純正の起動経路が正常で、Python 3 が使える対象機の Linux 上で実行します。
初回は TTL ログを取得できる状態で行ってください。
バンドルは対象機の永続ストレージに保存し、ディレクトリの権限を `0700` にします。

```sh
cd /path/to/qua-rescue-bundle
chmod 700 .
sudo python3 install-usb-chain.py . stage
sudo python3 install-usb-chain.py . arm
```

`stage` は元データを保存してから、専用の 512 セクター領域（LBA `0x33800`〜`0x339ff`）を書き込み、読み戻しで検証します。
`arm` は両方の純正環境バンクの `bootcmd` と `bootdelay` を設定します。
純正 ARM32 U-Boot は2秒、レスキュー U-Boot は3秒待機します。

CID、純正ブートローダー、起動用データ、環境設定などがプロファイルと一致しない場合は停止します。
失敗時は出力を保存し、値を書き換えて検証を回避しないでください。
バンドル内に作成される `*-before*.bin` と `*-result.json` は復元用に保管してください。

## USB コンソール

Qua Station の独立した USB 2.0 Type-A ポートを使用します。青色の USB 3.0 ポートではありません。
実機確認時の macOS デバイス名は次のとおりです。

```text
/dev/cu.usbmodemQuaRescue1
```

電源投入後、レスキュー U-Boot の3秒の受付中にターミナルからポートを開いてください。
CDC DTR が有効になるとコンソールに留まります。TTL からキー入力して中断することもできます。

macOS の例:

```sh
screen /dev/cu.usbmodemQuaRescue1 115200
```

プロンプトが表示されたら、次のコマンドを使用できます。

```text
version    バージョン表示
help       コマンド一覧
usbstat    USB の状態表示
boot       検証済みの通常 Linux 起動へ進む
```

コンソールを開かず、キー入力もしなければ3秒後に Linux の起動へ進みます。
Linux へ移行すると USB デバイスが切断・再認識され、ポート名も変わる場合があります。
Linux の `Gadget Serial v2.4` とレスキュー U-Boot のコンソールは別のデバイスです。
USB の列挙が不安定になる現象は継続調査事項です。

## インストール後の確認

Linux 起動後、`/proc/cmdline` にバンドルの nonce と一致する `qua_ram_chain=<nonce>` があることを確認します。
USB コンソールからの `version` / `boot` と、操作しない場合の自動起動をそれぞれ確認してください。

## 通常起動へ戻す

インストール時のバンドルを使い、対象機の Linux 上で実行します。

```sh
cd /path/to/qua-rescue-bundle
sudo python3 install-usb-chain.py . restore
```

両方の環境バンクをプロファイル内の通常起動コマンドと `bootdelay=2` に戻します。
専用レスキュー領域の内容とバックアップは保持します。
この操作で純正ブートローダーバイナリを書き換えることはありません。

Linux 自体が起動しない場合、この復元スクリプトは使用できません。
その場合の作業には、純正 U-Boot の TTL コンソールなど、独立したアクセス経路が必要です。
