#include <WiFi.h>
#include <esp_now.h>

uint8_t boardB_MAC[] = {0x1C, 0xC3, 0xAB, 0xA0, 0x6C, 0xC8};

typedef struct {
  uint32_t packetID;
  char message[32];
} DataPacket;

typedef struct {
  uint32_t ackedID;
} AckPacket;

DataPacket outPacket;
volatile bool ackReceived = false;
volatile uint32_t ackedID = 0;
uint32_t packetCounter = 0;

// Stats
uint32_t totalRTT = 0;
uint32_t packetsSent = 0;
unsigned long statsWindowStart = 0;
uint32_t packetsInWindow = 0;
uint32_t bytesInWindow = 0;

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send: FAILED");
  }
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  AckPacket ack;
  memcpy(&ack, data, sizeof(ack));
  ackedID = ack.ackedID;
  ackReceived = true;
}

void sendCommand(const char* cmd) {
  outPacket.packetID = packetCounter;
  snprintf(outPacket.message, sizeof(outPacket.message), "%s", cmd);

  ackReceived = false;
  unsigned long sendTime = micros();

  esp_now_send(boardB_MAC, (uint8_t *)&outPacket, sizeof(outPacket));

  // Wait for ACK with timeout of 100ms
  unsigned long timeout = millis();
  while (!ackReceived && (millis() - timeout < 100)) {
    delay(1);
  }

  if (ackReceived && ackedID == packetCounter) {
    unsigned long rtt = micros() - sendTime;
    totalRTT += rtt;
    packetsSent++;
    packetsInWindow++;
    bytesInWindow += sizeof(outPacket);

    Serial.print("PKT#"); Serial.print(packetCounter);
    Serial.print(" | CMD: "); Serial.print(cmd);
    Serial.print(" | RTT: "); Serial.print(rtt); Serial.print(" us");

    // Print stats every second
    if (millis() - statsWindowStart >= 1000) {
      float avgRTT = (float)totalRTT / packetsSent;
      Serial.print(" | Avg RTT: "); Serial.print(avgRTT); Serial.print(" us");
      Serial.print(" | PPS: "); Serial.print(packetsInWindow);
      Serial.print(" | BPS: "); Serial.print(bytesInWindow); Serial.print(" B/s");
      packetsInWindow = 0;
      bytesInWindow = 0;
      statsWindowStart = millis();
    }
    Serial.println();
  } else {
    Serial.print("PKT#"); Serial.print(packetCounter);
    Serial.print(" | CMD: "); Serial.print(cmd);
    Serial.println(" | TIMEOUT / NO ACK");
  }

  packetCounter++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, boardB_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }

  Serial.println("Board A ready.");
  Serial.println("Commands: LED:RED:ON, LED:RED:OFF, LED:YELLOW:ON, LED:YELLOW:OFF, LED:GREEN:ON, LED:GREEN:OFF");
  statsWindowStart = millis();
}

void loop() {
  // Read command from Serial Monitor
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0 && input.length() < 32) {
      sendCommand(input.c_str());
    } else if (input.length() >= 32) {
      Serial.println("Command too long! Max 31 characters.");
    }
  }
}