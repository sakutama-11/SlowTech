#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

const int photoSensorPin = 34;
const int diffuserPin = 25;
const int toggleButtonPin = 0;

const int photoThreshold = 20;
const bool presenceActiveHigh = true;
const bool useButtonToggleSimulation = false;
const unsigned long sensorDebounceMs = 120;
const unsigned long helloIntervalMs = 2000;
const unsigned long statusIntervalMs = 1000;
const unsigned long peerTimeoutMs = 15000;
const unsigned long buttonDebounceMs = 40;

const uint8_t diffuserChannel = 0;
const uint16_t diffuserPwmFreq = 5000;
const uint8_t diffuserPwmRes = 8;
const uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct EventRecord {
  String id;
  uint8_t state;
  unsigned long ts;
};

struct EspNowMessage {
  uint8_t msgType;
  uint32_t seq;
  uint8_t senderMac[6];
  uint8_t state;
};

enum MessageType : uint8_t {
  MSG_HELLO = 1,
  MSG_STATE = 2,
};

const size_t HISTORY_SIZE = 32;
EventRecord selfHistory[HISTORY_SIZE];
EventRecord othersHistory[HISTORY_SIZE];
size_t selfCount = 0;
size_t othersCount = 0;

bool pendingPresence = false;
bool stablePresence = false;
unsigned long pendingSinceMs = 0;
int lastButtonReading = HIGH;
int stableButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;

uint8_t selfMac[6] = {0};
uint8_t peerMac[6] = {0};
bool peerKnown = false;
unsigned long lastPeerSeenMs = 0;
unsigned long lastHelloMs = 0;
unsigned long lastStatusMs = 0;
uint32_t selfSeq = 0;

String macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool sameMac(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

void pushHistory(EventRecord* history, size_t& count, const String& id, uint8_t state) {
  if (count < HISTORY_SIZE) {
    history[count++] = {id, state, millis()};
    return;
  }

  for (size_t i = 1; i < HISTORY_SIZE; ++i) {
    history[i - 1] = history[i];
  }
  history[HISTORY_SIZE - 1] = {id, state, millis()};
}

uint8_t latestSelfState() {
  return selfCount == 0 ? 0 : selfHistory[selfCount - 1].state;
}

uint8_t latestOthersState() {
  return othersCount == 0 ? 0 : othersHistory[othersCount - 1].state;
}

void applyDiffuserRule() {
  uint8_t outputValue = (latestSelfState() == 1 && latestOthersState() == 1) ? 255 : 0;
  ledcWrite(diffuserChannel, outputValue);
}

bool readPresenceRaw() {
  int raw = analogRead(photoSensorPin);
  if (presenceActiveHigh) {
    return raw >= photoThreshold;
  }
  return raw <= photoThreshold;
}

void addPeerIfNeeded(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) {
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void rememberPeer(const uint8_t* mac) {
  if (sameMac(mac, selfMac)) {
    return;
  }

  memcpy(peerMac, mac, 6);
  peerKnown = true;
  lastPeerSeenMs = millis();
  addPeerIfNeeded(mac);
}

void handleRemoteState(const uint8_t* senderMac, uint8_t state) {
  rememberPeer(senderMac);
  pushHistory(othersHistory, othersCount, macToString(senderMac), state);
  applyDiffuserRule();

  Serial.print("[OTHER] id=");
  Serial.print(macToString(senderMac));
  Serial.print(" state=");
  Serial.println(state);
}

void sendMessage(const uint8_t* destination, MessageType msgType, uint8_t state) {
  EspNowMessage msg = {};
  msg.msgType = static_cast<uint8_t>(msgType);
  msg.seq = ++selfSeq;
  memcpy(msg.senderMac, selfMac, 6);
  msg.state = state;
  esp_now_send(destination, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
}

void sendHello() {
  sendMessage(broadcastMac, MSG_HELLO, latestSelfState());
}

void publishSelfEvent(uint8_t state) {
  pushHistory(selfHistory, selfCount, macToString(selfMac), state);
  applyDiffuserRule();

  Serial.print("[SELF] id=");
  Serial.print(macToString(selfMac));
  Serial.print(" state=");
  Serial.println(state);

  sendMessage(broadcastMac, MSG_STATE, state);
  if (peerKnown) {
    sendMessage(peerMac, MSG_STATE, state);
  }
}

void onDataSent(const uint8_t* macAddr, esp_now_send_status_t status) {
  (void)macAddr;
  (void)status;
}

void onDataRecv(const uint8_t* macAddr, const uint8_t* data, int dataLen) {
  if (dataLen != sizeof(EspNowMessage)) {
    return;
  }

  EspNowMessage msg;
  memcpy(&msg, data, sizeof(msg));

  if (sameMac(msg.senderMac, selfMac)) {
    return;
  }

  rememberPeer(msg.senderMac);

  if (msg.msgType == MSG_HELLO) {
    if (!peerKnown || sameMac(peerMac, msg.senderMac)) {
      sendMessage(peerMac, MSG_HELLO, latestSelfState());
    }
    return;
  }

  if (msg.msgType == MSG_STATE) {
    handleRemoteState(msg.senderMac, msg.state ? 1 : 0);
  }
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
    publishSelfEvent(stablePresence ? 1 : 0);
  }
}

void updateButtonToggle() {
  int reading = digitalRead(toggleButtonPin);
  unsigned long nowMs = millis();

  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChangeMs = nowMs;
  }

  if ((nowMs - lastButtonChangeMs) < buttonDebounceMs) {
    return;
  }

  if (reading != stableButtonState) {
    stableButtonState = reading;

    // GPIO0 button is active low; toggle self state on single press.
    if (stableButtonState == LOW) {
      stablePresence = !stablePresence;
      pendingPresence = stablePresence;
      pendingSinceMs = nowMs;
      publishSelfEvent(stablePresence ? 1 : 0);

      Serial.print("[BUTTON] toggle self=");
      Serial.println(stablePresence ? 1 : 0);
    }
  }
}

void maintainPeerDiscovery() {
  unsigned long now = millis();

  if (now - lastHelloMs >= helloIntervalMs) {
    lastHelloMs = now;
    sendHello();
  }

  if (peerKnown && (now - lastPeerSeenMs) > peerTimeoutMs) {
    peerKnown = false;
    memset(peerMac, 0, sizeof(peerMac));
    othersCount = 0;
    applyDiffuserRule();
    Serial.println("[PEER] timed out");
  }
}

void printStatusPeriodically() {
  unsigned long now = millis();
  if (now - lastStatusMs < statusIntervalMs) {
    return;
  }
  lastStatusMs = now;

  int raw = analogRead(photoSensorPin);
  Serial.print("[STATUS] self=");
  Serial.print((int)latestSelfState());
  Serial.print(" other=");
  Serial.print((int)latestOthersState());
  Serial.print(" diffuser=");
  Serial.print((latestSelfState() == 1 && latestOthersState() == 1) ? 1 : 0);
  Serial.print(" peer=");
  Serial.print(peerKnown ? macToString(peerMac) : String("none"));
  Serial.print(" sensorRaw=");
  Serial.println(raw);
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) {
      delay(1000);
    }
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, broadcastMac, 6);
  broadcastPeer.channel = 0;
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.encrypt = false;
  if (!esp_now_is_peer_exist(broadcastMac)) {
    esp_now_add_peer(&broadcastPeer);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(photoSensorPin, INPUT);
  pinMode(toggleButtonPin, INPUT_PULLUP);
  ledcSetup(diffuserChannel, diffuserPwmFreq, diffuserPwmRes);
  ledcAttachPin(diffuserPin, diffuserChannel);
  ledcWrite(diffuserChannel, 0);

  analogReadResolution(12);
  setupEspNow();
  WiFi.macAddress(selfMac);

  Serial.print("Boot MAC=");
  Serial.println(macToString(selfMac));

  stablePresence = useButtonToggleSimulation ? false : readPresenceRaw();
  pendingPresence = stablePresence;
  pendingSinceMs = millis();
  publishSelfEvent(stablePresence ? 1 : 0);
  sendHello();
}

void loop() {
  maintainPeerDiscovery();
  if (useButtonToggleSimulation) {
    updateButtonToggle();
  } else {
    updatePresenceState();
  }
  applyDiffuserRule();
  printStatusPeriodically();
  delay(20);
}
