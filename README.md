# Yohakō（仮）プロトタイプ 開発ドキュメント

Yohakō は、Arduino Uno R4 WiFi と Web アプリ（Next.js / Vercel）を BLE で接続し、香り出力と「誰が置いたか」の記録を行うプロトタイプです。

## 1. 現在の構成

- フロントエンド: Next.js（App Router）
- デプロイ先: Vercel
- 記録ストア: Vercel KV
- 通信: BLE（Web Bluetooth API）
- 検知: フォトリフレクタ（Arduino 側）
- 出力: 5V 小型 DC ファン 3 基（PWM 制御）

## 2. BLE は双方向通信

このプロトタイプでは BLE を双方向で使っています。

- Web -> Arduino
  - 香りデータ（3 バイト）送信
  - 停止コマンド（0,0,0）送信
- Arduino -> Web
  - フォトリフレクタ検知結果を Notify（1 バイト）
  - 1: 置いた
  - 0: 外した

## 3. Arduino 側実装

ファイル: `arduino/src/main.cpp`

### 3.1 BLE キャラクタリスティック

- `19B10001-E8F2-537E-4F6C-D104768A1214`
  - 種別: `BLERead | BLEWrite`
  - 用途: 香りデータ受信（3 バイト）
- `19B10002-E8F2-537E-4F6C-D104768A1214`
  - 種別: `BLERead | BLENotify`
  - 用途: 配置状態通知（1 バイト）

### 3.2 フォトリフレクタ設定

- センサーピン: `A0`
- しきい値: `500`
- デバウンス: `120ms`

### 3.3 動作

1. `loop()` で `BLE.poll()` を実行
2. フォトリフレクタの値を読み取り
3. 状態変化がデバウンス後に確定したら Notify を送信
4. Web からの 3 バイト香りデータを受信したら PWM 更新
5. 停止コマンド（0,0,0）受信で全ファン停止

## 4. Web 側実装（Next.js）

ファイル: `web/app/page.tsx`

### 4.1 動作

1. ユーザーが「誰が置いたか」を入力
2. 「余白の時間を始める」で `Yohako-01` に BLE 接続
3. 香りデータを送信
4. 配置状態 Notify を購読（`startNotifications()`）
5. Notify 受信時
   - `1 (placed)`: 香りデータ送信、イベント保存
   - `0 (removed)`: 停止コマンド送信、イベント保存

### 4.2 記録 API

ファイル: `web/app/api/place/route.ts`

- `GET /api/place`
  - 最新イベントを返却
- `POST /api/place`
  - `placedBy` と `action`（`placed` or `removed`）を保存
  - 保存先
    - 最新: `yohako:place:latest`
    - 履歴: `yohako:place:history`（最大 100 件）

## 5. ハードウェア配線（概要）

### 5.1 ファン駆動

- Arduino `3,5,6` -> 各 MOSFET Gate
- 各 MOSFET Source -> GND
- 各 MOSFET Drain -> 各ファンのマイナス
- 各ファンのプラス -> 外部 5V
- ファン逆起電力対策としてダイオードを並列挿入

### 5.2 フォトリフレクタ

- センサー出力 -> Arduino `A0`
- VCC / GND は使用モジュール仕様に従って配線
- Arduino GND と外部電源 GND は必ず共通化

## 6. セットアップ

### 6.1 Arduino

`arduino/platformio.ini`

```ini
[env:uno_r4_wifi]
platform = renesas-ra
board = uno_r4_wifi
framework = arduino
lib_deps =
  arduino-libraries/ArduinoBLE@2.1.0
```

### 6.2 Web（ローカル）

```bash
cd web
npm install
npm run dev
```

### 6.3 Vercel

1. VercelでこのリポジトリをImportする
2. Project Settings > General > Root Directoryで`web`を指定する
3. Framework Presetが`Next.js`、Build Commandが`npm run build`になっていることを確認する
4. StorageからRedis（旧Vercel KV）を作成し、このプロジェクトに接続する
5. Environment Variablesに`KV_REST_API_URL`と`KV_REST_API_TOKEN`が追加されていることを確認する
6. Deployを実行する
7. HTTPSの本番URLをAndroid Chromeから開いて動作確認する

ローカルでは、`web/.env.example`を`web/.env.local`としてコピーし、Redis接続情報を設定する。`.env.local`はGitにコミットしないこと。

Vercel CLIを使う場合は、リポジトリのルートで次を実行する。

```bash
cd web
npx vercel
npx vercel --prod
```

CLIでもRoot Directoryを`web`として扱うため、Vercelダッシュボードから設定する場合と同じ構成になる。

## 7. 注意点

- Web Bluetooth はユーザー操作を伴う接続開始が必要
- Notify は接続中のみ受信可能
- フォトリフレクタのしきい値は現場光で要調整（`photoThreshold`）
