#include <WiFi.h>
#include <esp_now.h>

#define LED_RED     27
#define LED_YELLOW  33
#define LED_GREEN   13

uint8_t boardA_MAC[] = {0xD4, 0xE9, 0xF4, 0x78, 0xE9, 0x70};

typedef struct {
  uint32_t packetID;
  char message[32];
} DataPacket;

typedef struct {
  uint32_t ackedID;
} AckPacket;

void executeLEDCommand(const char* cmd) {
  // Parse format: LED:<COLOR>:<STATE>
  String command = String(cmd);

  if (command == "LED:RED:ON")         digitalWrite(LED_RED, HIGH);
  else if (command == "LED:RED:OFF")   digitalWrite(LED_RED, LOW);
  else if (command == "LED:YELLOW:ON") digitalWrite(LED_YELLOW, HIGH);
  else if (command == "LED:YELLOW:OFF")digitalWrite(LED_YELLOW, LOW);
  else if (command == "LED:GREEN:ON")  digitalWrite(LED_GREEN, HIGH);
  else if (command == "LED:GREEN:OFF") digitalWrite(LED_GREEN, LOW);
  else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
  }
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  DataPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  Serial.print("PKT#"); Serial.print(incoming.packetID);
  Serial.print(" | CMD: "); Serial.println(incoming.message);

  // Execute the command
  executeLEDCommand(incoming.message);

  // Send ACK back
  AckPacket ack;
  ack.ackedID = incoming.packetID;
  esp_now_send(boardA_MAC, (uint8_t *)&ack, sizeof(ack));
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  // All LEDs off at start
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);

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

  Serial.println("Board B ready. Waiting for commands...");
}

void loop() {}