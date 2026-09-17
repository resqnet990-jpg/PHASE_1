/*
  ResQNet RAMP — Node ESP32 (Relay only)

  - Random 50-200ms boot delay before joining (robustness pattern).
  - Sends HELLO, then determines its mode after a discovery window:
      -> if a downstream HELLO is seen (e.g. from the Robot ESP, or another
         relay further down the chain) -> locks as RELAY.
      -> if nothing is seen -> IDLE (nothing attached below it yet; does
         not forward, just waits — this is NOT the robot role).
  - Locks its upstream neighbor (Master, or another relay above it) only
    after 10 confirming NET_SYNC packets from the same source (robustness
    against noise).
  - Forwards using the FLAGS direction bit + 16-entry dedup buffer.
  - This sketch never drives LEDs or takes actions — that behavior lives
    in the dedicated Robot ESP sketch.

  Requires: ramp_common.h in the same sketch folder (or Arduino libraries path).
*/

#include <WiFi.h>
#include <esp_now.h>
#include "ramp_common.h"

#define MY_NODE_ID NODE_ID_NODE1

DedupBuffer dedup;

uint8_t currentRole = ROLE_NODE_UNSET;

// Upstream neighbor locking
uint8_t upstreamMac[6];
bool upstreamKnown = false;
bool upstreamLocked = false;
uint8_t upstreamConfirmCount = 0;
const uint8_t UPSTREAM_LOCK_THRESHOLD = 10;

// Discovery window — decides RELAY vs IDLE
unsigned long discoveryStart = 0;
const unsigned long DISCOVERY_WINDOW_MS = 3000;
bool discoveryDone = false;

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

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < RAMP_HEADER_LEN) return;
  RampPacket pkt;
  memcpy(&pkt, incomingData, len);
  const uint8_t *senderMac = info->src_addr;

  switch (pkt.type) {

    case PKT_HELLO: {
      // A HELLO from someone else means a node/robot exists downstream of
      // us -> we are not idle, we relay.
      if (currentRole == ROLE_NODE_UNSET || currentRole == ROLE_NODE_IDLE) {
        currentRole = ROLE_NODE_RELAY;
        Serial.println("[ROLE] downstream HELLO seen -> RELAY");
      }
      // Also reply so the downstream device can lock us as its upstream.
      RampPacket ack;
      ack.type = PKT_HELLO_ACK;
      ack.flags = FLAG_DIR_DOWNSTREAM;
      ack.source_id = MY_NODE_ID;
      ack.dest_id = pkt.source_id;
      ack.seq_num = 0;
      ack.payload_len = 0;
      sendRampPacket(senderMac, &ack);
      break;
    }

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
      // Re-broadcast NET_SYNC downstream so nodes/robot further down can
      // also hear it (single-hop ESP-NOW range only).
      broadcastRampPacket(&pkt);
      break;
    }

    case PKT_INSTRUCTION:
    case PKT_FEEDBACK: {
      if (dedup.isDuplicate(pkt.source_id, pkt.seq_num)) return;
      dedup.markSeen(pkt.source_id, pkt.seq_num);

      if (currentRole == ROLE_NODE_RELAY) {
        broadcastRampPacket(&pkt);
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

  randomSeed(analogRead(0));
  delay(random(50, 201));

  Serial.println("=== ResQNet Node ESP (Relay) ===");

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  addPeerIfNeeded(BROADCAST_MAC);

  sendHello();
  discoveryStart = millis();
}

void loop() {
  if (!discoveryDone && millis() - discoveryStart >= DISCOVERY_WINDOW_MS) {
    discoveryDone = true;
    if (currentRole == ROLE_NODE_UNSET) {
      currentRole = ROLE_NODE_IDLE;
      Serial.println("[ROLE] no downstream neighbors -> IDLE (nothing attached yet)");
    }
  }
}
