#include <WiFi.h>
#include <esp_now.h>

#define LED_PIN 3

// Board B's MAC address
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

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // optional: can log send failures here
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  AckPacket ack;
  memcpy(&ack, data, sizeof(ack));
  ackedID = ack.ackedID;
  ackReceived = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
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

  Serial.println("Board A ready. Starting speed test...");
  statsWindowStart = millis();
}

void loop() {
  outPacket.packetID = packetCounter;
  snprintf(outPacket.message, sizeof(outPacket.message), "PKT#%lu", packetCounter);

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

    Serial.print("PKT#"); Serial.print(packetCounter);
    Serial.print(" | RTT: "); Serial.print(rtt); Serial.print(" us");

    // Print stats every second
    if (millis() - statsWindowStart >= 1000) {
      float avgRTT = (float)totalRTT / packetsSent;
      Serial.print(" | Avg RTT: "); Serial.print(avgRTT); Serial.print(" us");
      Serial.print(" | PPS: "); Serial.print(packetsInWindow);
      packetsInWindow = 0;
      statsWindowStart = millis();
    }
    Serial.println();
  } else {
    Serial.print("PKT#"); Serial.print(packetCounter);
    Serial.println(" | TIMEOUT / NO ACK");
  }

  packetCounter++;
}