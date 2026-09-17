#include <WiFi.h>
#include <esp_now.h>

#define LED_PIN 3

// Board A's MAC address
uint8_t boardA_MAC[] = {0xD4, 0xE9, 0xF4, 0x78, 0xE9, 0x70};

typedef struct {
  uint32_t packetID;
  char message[32];
} DataPacket;

typedef struct {
  uint32_t ackedID;
} AckPacket;

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  DataPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  Serial.print("Received PKT#"); Serial.print(incoming.packetID);
  Serial.print(" | Msg: "); Serial.println(incoming.message);

  // Send ACK back
  AckPacket ack;
  ack.ackedID = incoming.packetID;
  esp_now_send(boardA_MAC, (uint8_t *)&ack, sizeof(ack));

  // Blink LED on receive
  digitalWrite(LED_PIN, HIGH);
  delay(10);
  digitalWrite(LED_PIN, LOW);
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

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, boardA_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer!");
    return;
  }

  Serial.println("Board B ready. Waiting for packets...");
}

void loop() {}