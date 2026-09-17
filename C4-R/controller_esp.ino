/*
  ResQNet RAMP — Controller ESP32
  Bridges laptop (Serial) <-> RAMP mesh (ESP-NOW).

  - Reads text commands from Serial, wraps as PKT_INSTRUCTION (upstream),
    broadcasts so Master picks it up (no Master MAC hardcoding needed).
  - Receives PKT_FEEDBACK (downstream) from the mesh, prints to Serial.

  Serial command format (type into Serial Monitor, newline-terminated):
      <dest_id> <text payload>
  e.g.
      2 LED_ON
      2 BLINK

  Requires: ramp_common.h in the same sketch folder (or Arduino libraries path).
*/

#include <WiFi.h>
#include <esp_now.h>
#include "ramp_common.h"

uint8_t seqCounter = 0;

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < RAMP_HEADER_LEN) return;
  RampPacket pkt;
  memcpy(&pkt, incomingData, len);

  if (pkt.type == PKT_FEEDBACK && GET_DIR(pkt.flags) == FLAG_DIR_DOWNSTREAM) {
    Serial.print("[FEEDBACK] from node ");
    Serial.print(pkt.source_id);
    Serial.print(" seq ");
    Serial.print(pkt.seq_num);
    Serial.print(": ");
    for (int i = 0; i < pkt.payload_len; i++) Serial.write(pkt.payload[i]);
    Serial.println();
  }
}

void sendInstruction(uint8_t destId, const char *text) {
  RampPacket pkt;
  pkt.type = PKT_INSTRUCTION;
  pkt.flags = FLAG_DIR_UPSTREAM;
  pkt.source_id = NODE_ID_CONTROLLER;
  pkt.dest_id = destId;
  pkt.seq_num = seqCounter++;
  pkt.payload_len = min((int)strlen(text), RAMP_MAX_PAYLOAD);
  memcpy(pkt.payload, text, pkt.payload_len);

  broadcastRampPacket(&pkt);

  Serial.print("[SENT] instruction -> node ");
  Serial.print(destId);
  Serial.print(" seq ");
  Serial.println(pkt.seq_num);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== ResQNet Controller ESP ===");

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  addPeerIfNeeded(BROADCAST_MAC);

  Serial.println("Ready. Type: <dest_id> <text>   e.g.  2 LED_ON");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    int spaceIdx = line.indexOf(' ');
    if (spaceIdx == -1) {
      Serial.println("Format: <dest_id> <text>");
      return;
    }

    uint8_t destId = (uint8_t)line.substring(0, spaceIdx).toInt();
    String text = line.substring(spaceIdx + 1);
    sendInstruction(destId, text.c_str());
  }
}
