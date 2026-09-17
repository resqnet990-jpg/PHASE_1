/*
  ResQNet RAMP — Robot ESP32

  Dedicated firmware for the robot-side node. Always a leaf/terminal by
  definition (nothing is ever downstream of the robot), so no dynamic
  mode-select is needed here — that's only for relay Nodes.

  CURRENT STAGE: responds to INSTRUCTION packets by blinking/setting 3
  LEDs and sending FEEDBACK back upstream. Motor driving is NOT
  implemented yet — driveOutputForCommand() is the single place to
  extend into real robot control later.

  - Random 50-200ms boot delay before joining (robustness pattern).
  - Sends HELLO upstream, waits for HELLO_ACK to find its upstream
    neighbor (a relay Node, or Master directly).
  - Locks that upstream neighbor only after 10 confirming NET_SYNC
    packets from the same source (robustness against noise).
  - On INSTRUCTION addressed to it: drives LEDs, sends FEEDBACK.
  - Uses the 16-entry dedup buffer like every other node in the mesh.

  Requires: ramp_common.h in the same sketch folder (or Arduino libraries path).
*/

#include <WiFi.h>
#include <esp_now.h>
#include "ramp_common.h"

// LED pins — GPIO 33 is input-only on some ESP32 variants; swap to 25/26
// if it doesn't respond on your board (see earlier Stage 2 notes).
#define LED1_PIN 13
#define LED2_PIN 33
#define LED3_PIN 27

#define MY_NODE_ID NODE_ID_ROBOT

DedupBuffer dedup;

// Upstream neighbor locking
uint8_t upstreamMac[6];
bool upstreamKnown = false;
bool upstreamLocked = false;
uint8_t upstreamConfirmCount = 0;
const uint8_t UPSTREAM_LOCK_THRESHOLD = 10;

void sendHello() {
  RampPacket pkt;
  pkt.type = PKT_HELLO;
  pkt.flags = FLAG_DIR_UPSTREAM;
  pkt.source_id = MY_NODE_ID;
  pkt.dest_id = NODE_ID_BROADCAST;
  pkt.seq_num = 0;
  pkt.payload_len = 0;
  broadcastRampPacket(&pkt);
  Serial.println("[HELLO] sent");
}

// ---- Single place to extend into real robot control later ----
void driveOutputForCommand(const uint8_t *payload, int len) {
  String text = "";
  for (int i = 0; i < len; i++) text += (char)payload[i];
  text.trim();

  Serial.print("[ROBOT] command: ");
  Serial.println(text);

  // Placeholder mapping — LEDs stand in for motor commands for now.
  if (text == "LED_ON") {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    digitalWrite(LED3_PIN, HIGH);
  } else if (text == "LED_OFF") {
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);
  } else if (text == "BLINK") {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED1_PIN, HIGH); delay(150); digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, HIGH); delay(150); digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, HIGH); delay(150); digitalWrite(LED3_PIN, LOW);
    }
  }
  // TODO (later): FORWARD / REVERSE / TURN_LEFT / TURN_RIGHT / STOP ->
  // motor driver calls, once the robot chassis firmware is scoped.
}

void sendFeedback(uint8_t seqOfInstruction, const char *msg) {
  RampPacket pkt;
  pkt.type = PKT_FEEDBACK;
  pkt.flags = FLAG_DIR_DOWNSTREAM;
  pkt.source_id = MY_NODE_ID;
  pkt.dest_id = NODE_ID_CONTROLLER;
  pkt.seq_num = seqOfInstruction;
  pkt.payload_len = min((int)strlen(msg), RAMP_MAX_PAYLOAD);
  memcpy(pkt.payload, msg, pkt.payload_len);

  if (upstreamLocked) {
    sendRampPacket(upstreamMac, &pkt);
  } else {
    broadcastRampPacket(&pkt);
  }
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < RAMP_HEADER_LEN) return;
  RampPacket pkt;
  memcpy(&pkt, incomingData, len);
  const uint8_t *senderMac = info->src_addr;

  switch (pkt.type) {

    case PKT_HELLO_ACK: {
      if (!upstreamKnown) {
        memcpy(upstreamMac, senderMac, 6);
        upstreamKnown = true;
        Serial.print("[UPSTREAM] candidate found: ");
        printMac(senderMac);
        Serial.println();
      }
      break;
    }

    case PKT_NET_SYNC: {
      if (upstreamKnown && !upstreamLocked && memcmp(senderMac, upstreamMac, 6) == 0) {
        upstreamConfirmCount++;
        if (upstreamConfirmCount >= UPSTREAM_LOCK_THRESHOLD) {
          upstreamLocked = true;
          Serial.println("[UPSTREAM] locked after 10 confirmations");
        }
      }
      break;
    }

    case PKT_INSTRUCTION: {
      if (dedup.isDuplicate(pkt.source_id, pkt.seq_num)) return;
      dedup.markSeen(pkt.source_id, pkt.seq_num);

      if (pkt.dest_id == MY_NODE_ID || pkt.dest_id == NODE_ID_BROADCAST) {
        driveOutputForCommand(pkt.payload, pkt.payload_len);
        sendFeedback(pkt.seq_num, "OK");
      }
      break;
    }

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  // Robustness pattern: random 50-200ms boot delay before joining the mesh.
  randomSeed(analogRead(0));
  delay(random(50, 201));

  Serial.println("=== ResQNet Robot ESP ===");

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  addPeerIfNeeded(BROADCAST_MAC);

  sendHello();
}

void loop() {
  // Nothing periodic yet — everything is event-driven off the ESP-NOW callback.
}
