/*
  ResQNet RAMP — Master ESP32

  - Answers HELLO / LINK_QUERY locally (never forwarded further).
  - Broadcasts PKT_NET_SYNC periodically (simple beacon for now — full TDMA
    slot assignment is a later iteration).
  - Forwards PKT_INSTRUCTION (upstream, from Controller) into the mesh.
  - Forwards PKT_FEEDBACK (downstream, from a node) back toward Controller.
  - Master is treated as non-failing by design — no self-recovery logic here;
    a downed Master is replaced by a human, not routed around.

  Requires: ramp_common.h in the same sketch folder (or Arduino libraries path).
*/

#include <WiFi.h>
#include <esp_now.h>
#include "ramp_common.h"

DedupBuffer dedup;
unsigned long lastSyncTime = 0;
const unsigned long SYNC_INTERVAL_MS = 2000;

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < RAMP_HEADER_LEN) return;
  RampPacket pkt;
  memcpy(&pkt, incomingData, len);
  const uint8_t *senderMac = info->src_addr;

  switch (pkt.type) {

    case PKT_HELLO: {
      // Local-only: reply directly to sender, never forwarded further.
      Serial.print("[HELLO] from ");
      printMac(senderMac);
      Serial.println();

      RampPacket ack;
      ack.type = PKT_HELLO_ACK;
      ack.flags = FLAG_DIR_DOWNSTREAM;
      ack.source_id = NODE_ID_MASTER;
      ack.dest_id = pkt.source_id;
      ack.seq_num = 0;
      ack.payload_len = 0;
      sendRampPacket(senderMac, &ack);
      break;
    }

    case PKT_LINK_QUERY: {
      // Local-only stub — real response logic deferred to topology-transition work.
      Serial.println("[LINK_QUERY] received (stub, no response logic yet)");
      break;
    }

    case PKT_INSTRUCTION: {
      if (dedup.isDuplicate(pkt.source_id, pkt.seq_num)) return;
      dedup.markSeen(pkt.source_id, pkt.seq_num);

      Serial.print("[INSTRUCTION] from controller, dest=");
      Serial.print(pkt.dest_id);
      Serial.print(" seq=");
      Serial.println(pkt.seq_num);

      broadcastRampPacket(&pkt); // flood downstream toward nodes
      break;
    }

    case PKT_FEEDBACK: {
      if (dedup.isDuplicate(pkt.source_id, pkt.seq_num)) return;
      dedup.markSeen(pkt.source_id, pkt.seq_num);

      Serial.print("[FEEDBACK] from node ");
      Serial.print(pkt.source_id);
      Serial.print(" seq=");
      Serial.println(pkt.seq_num);

      broadcastRampPacket(&pkt); // relay toward Controller
      break;
    }

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== ResQNet Master ESP ===");

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  addPeerIfNeeded(BROADCAST_MAC);

  Serial.println("Master ready.");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSyncTime >= SYNC_INTERVAL_MS) {
    lastSyncTime = now;

    RampPacket sync;
    sync.type = PKT_NET_SYNC;
    sync.flags = FLAG_DIR_DOWNSTREAM;
    sync.source_id = NODE_ID_MASTER;
    sync.dest_id = NODE_ID_BROADCAST;
    sync.seq_num = 0;
    sync.payload_len = 0;
    broadcastRampPacket(&sync);
  }
}
