#include <ArduinoBLE.h>

// BLEサービスとキャラクタリスティックのUUID（任意のUUIDでOK）
BLEService scentService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic scentCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, 3); // 3バイトのデータを受信
BLEByteCharacteristic presenceCharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify); // 1:置いた 0:外した

// ファンのPWMピン
const int fanPin1 = 3;
const int fanPin2 = 5;
const int fanPin3 = 6;

// フォトリフレクタ設定
const int photoSensorPin = A0;
const int photoThreshold = 500;
const bool presenceActiveHigh = true;
const unsigned long sensorDebounceMs = 120;

bool pendingPresence = false;
bool stablePresence = false;
unsigned long pendingSinceMs = 0;

// 現在のファン状態（各ファンのPWM値: 0-255）
struct FanState {
  uint8_t fan1;
  uint8_t fan2;
  uint8_t fan3;
} currentFanState = {0, 0, 0};

// 関数プロトタイプ宣言
void onScentDataReceived(BLEDevice central, BLECharacteristic characteristic);
bool isStopCommand(const uint8_t* data);
void handleScentData(const uint8_t* data);
void handleStopCommand();
void applyFanState();
void updatePresenceState();
bool readPresenceRaw();
void publishPresenceState(bool isPlaced);

void setup() {
  Serial.begin(9600);
  while (!Serial);  // シリアルモニタが開くまで待機（デバッグ用 - 不要になったら削除）
  
  pinMode(fanPin1, OUTPUT);
  pinMode(fanPin2, OUTPUT);
  pinMode(fanPin3, OUTPUT);
  pinMode(photoSensorPin, INPUT);

  if (!BLE.begin()) {
    Serial.println("BLEの初期化に失敗しました。");
    while (1);
  }

  // デバイス名の設定（Web側からこの名前を探す）
  BLE.setLocalName("Yohako-01");
  BLE.setAdvertisedService(scentService);

  scentService.addCharacteristic(scentCharacteristic);
  scentService.addCharacteristic(presenceCharacteristic);
  BLE.addService(scentService);

  // データ受信時のコールバックを設定
  scentCharacteristic.setEventHandler(BLEWritten, onScentDataReceived);

  // BLEアドバタイズ開始
  BLE.advertise();
  Serial.println("BLE待機中... デバイス名: Yohako-01");

  // センサー状態を初期化し、接続直後に読めるよう現在状態を反映
  stablePresence = readPresenceRaw();
  pendingPresence = stablePresence;
  pendingSinceMs = millis();
  publishPresenceState(stablePresence);
}

void loop() {
  // [DEBUG] 1秒ごとにファンをON/OFFして基板接続確認（デバッグ用 - 不要になったら削除）
  static bool fanDebugOn = false;
  static unsigned long lastFanToggleMs = 0;
  unsigned long nowLoop = millis();
  if (nowLoop - lastFanToggleMs >= 1000) {
    lastFanToggleMs = nowLoop;
    fanDebugOn = !fanDebugOn;
    uint8_t val = fanDebugOn ? 200 : 0;
    analogWrite(fanPin1, val);
    analogWrite(fanPin2, val);
    analogWrite(fanPin3, val);
    Serial.print("[DEBUG] fan=");
    Serial.println(fanDebugOn ? "ON" : "OFF");
  }

  // BLEのポーリング処理
  BLE.poll();
  
  // 現在のファン状態を反映
  applyFanState();

  // フォトリフレクタから配置状態を監視し、変化時のみ通知
  updatePresenceState();
}

// Webアプリからデータを受信した時に呼ばれる処理
void onScentDataReceived(BLEDevice central, BLECharacteristic characteristic) {
  const uint8_t* data = characteristic.value();
  
  if (characteristic.valueLength() == 3) {
    // 受信したデータをファン状態に変換
    if (isStopCommand(data)) {
      handleStopCommand();
    } else {
      handleScentData(data);
    }
  }
}

// 停止コマンド（0,0,0）かどうかを判定
bool isStopCommand(const uint8_t* data) {
  return (data[0] == 0 && data[1] == 0 && data[2] == 0);
}

// 香りデータを受信した時の処理
void handleScentData(const uint8_t* data) {
  currentFanState.fan1 = data[0];
  currentFanState.fan2 = data[1];
  currentFanState.fan3 = data[2];

  Serial.print("香りデータ受信 - Fan1: "); Serial.print(currentFanState.fan1);
  Serial.print(" Fan2: "); Serial.print(currentFanState.fan2);
  Serial.print(" Fan3: "); Serial.println(currentFanState.fan3);
}

// 停止コマンドを受信した時の処理
void handleStopCommand() {
  currentFanState.fan1 = 0;
  currentFanState.fan2 = 0;
  currentFanState.fan3 = 0;
  
  Serial.println("停止コマンド受信 - すべてのファンを停止します。");
}

// 現在のファン状態をハードウェアに反映
void applyFanState() {
  analogWrite(fanPin1, currentFanState.fan1);
  analogWrite(fanPin2, currentFanState.fan2);
  analogWrite(fanPin3, currentFanState.fan3);
}

bool readPresenceRaw() {
  int raw = analogRead(photoSensorPin);

  // [DEBUG] フォトリフレクタ生値をシリアルモニタに出力（デバッグ用 - 不要になったら削除）
  static unsigned long lastDebugMs = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastDebugMs >= 200) {
    lastDebugMs = nowMs;
    Serial.print("[DEBUG] photoSensor raw=");
    Serial.print(raw);
    Serial.print(" threshold=");
    Serial.print(photoThreshold);
    Serial.print(" detected=");
    Serial.println((presenceActiveHigh ? raw >= photoThreshold : raw <= photoThreshold) ? "YES" : "NO");
  }

  if (presenceActiveHigh) {
    return raw >= photoThreshold;
  }
  return raw <= photoThreshold;
}

void updatePresenceState() {
  bool rawPresence = readPresenceRaw();
  unsigned long nowMs = millis();

  if (rawPresence != pendingPresence) {
    pendingPresence = rawPresence;
    pendingSinceMs = nowMs;
    return;
  }

  if ((nowMs - pendingSinceMs) >= sensorDebounceMs && stablePresence != pendingPresence) {
    stablePresence = pendingPresence;
    publishPresenceState(stablePresence);
  }
}

void publishPresenceState(bool isPlaced) {
  uint8_t value = isPlaced ? 1 : 0;
  presenceCharacteristic.writeValue(value);

  if (isPlaced) {
    Serial.println("配置検知: 置いた");
  } else {
    Serial.println("配置検知: 外した");
  }
}