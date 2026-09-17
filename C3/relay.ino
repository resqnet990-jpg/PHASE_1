/*
 * relay.ino
 * ResQNet — Multi-hop Range & Delay Test
 *
 * Role: DUMB RELAY (forwarder only)
 *
 * Behaviour:
 *   - Knows exactly two peers: SENDER_RECEIVER and RECEIVER.
 *     In a 2-board test, both MACs are the same board.
 *     In a 3-board test, set RECEIVER_MAC to the third ESP.
 *   - On every received packet: rebroadcast to the OTHER peer.
 *     Source is determined by comparing sender MAC to known MACs.
 *   - No RAMP logic, no TTL check, no dedup.
 *     This is intentional — we're testing range and delay, not protocol.
 *
 * To upgrade to a proper RAMP relay later:
 *   - Add TTL decrement (byte at offset 1 if you add TTL to header).
 *   - Add dedup buffer keyed on (src_mac + seq).
 *   - Add hop_count increment.
 *
 * Packet format (shared with sender_receiver.ino):
 *   [type:1][seq:1][src_mac:6][dst_mac:6][payload:N]
 */

#include <esp_now.h>
#include <WiFi.h>

// ─────────────────────────────────────────────
//  CONFIGURE MAC ADDRESSES HERE
// ─────────────────────────────────────────────

// MAC of the SENDER+RECEIVER board
uint8_t SENDER_RECEIVER_MAC[6] = {0x1C,0xC3,0xAB,0xA0,0x6C,0xC8};

// MAC of the RECEIVER board.
// 2-board test : set this to the same MAC as SENDER_RECEIVER_MAC.
// 3-board test : set this to the third ESP's MAC.
uint8_t RECEIVER_MAC[6]        = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─────────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────────
bool macEqual(const uint8_t* a, const uint8_t* b)
{
  return memcmp(a, b, 6) == 0;
}

void printMac(const uint8_t* mac)
{
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ─────────────────────────────────────────────
//  ESP-NOW CALLBACKS
// ─────────────────────────────────────────────
void onSendCallback(const wifi_tx_info_t* info, esp_now_send_status_t status)
{
  Serial.printf("[RELAY][TX] Forward — %s\n",
    (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "FAILED");
}

void onReceiveCallback(const esp_now_recv_info_t* info,
                       const uint8_t* data, int len)
{
  const uint8_t* senderMac = info->src_addr;
  int8_t rssi = info->rx_ctrl->rssi;

  Serial.printf("[RELAY][RX] %d bytes from ", len);
  printMac(senderMac);
  Serial.printf("  RSSI=%ddBm  type=0x%02X seq=0x%02X\n",
    rssi, data[0], data[1]);

  // Decide forward target: whoever is NOT the sender
  uint8_t* forwardTo = nullptr;

  if (macEqual(senderMac, SENDER_RECEIVER_MAC)) {
    // Packet came from SENDER side → forward to RECEIVER side
    forwardTo = RECEIVER_MAC;
    Serial.println("[RELAY] Direction: SENDER → RECEIVER");
  }
  else if (macEqual(senderMac, RECEIVER_MAC)) {
    // Packet came from RECEIVER side → forward to SENDER side
    forwardTo = SENDER_RECEIVER_MAC;
    Serial.println("[RELAY] Direction: RECEIVER → SENDER");
  }
  else {
    Serial.println("[RELAY] Unknown sender MAC — dropping packet.");
    return;
  }

  // Forward immediately (dumb relay — no modification)
  esp_err_t err = esp_now_send(forwardTo, data, len);
  if (err != ESP_OK)
    Serial.printf("[RELAY] Forward error: %d\n", err);
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  delay(500);

  // Print this board's MAC (relay doesn't need to know its own MAC, but useful)
  WiFi.mode(WIFI_STA);
  uint8_t myMac[6];
  WiFi.macAddress(myMac);
  Serial.printf("\n[BOOT] RELAY MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed. Halting.");
    while (true) {}
  }

  esp_now_register_send_cb(onSendCallback);
  esp_now_register_recv_cb(onReceiveCallback);

  // Register SENDER_RECEIVER as peer
  esp_now_peer_info_t peer1 = {};
  memcpy(peer1.peer_addr, SENDER_RECEIVER_MAC, 6);
  peer1.channel = 0;
  peer1.encrypt = false;
  if (esp_now_add_peer(&peer1) != ESP_OK)
    Serial.println("[ERROR] Failed to add SENDER_RECEIVER peer.");
  else
    Serial.println("[BOOT] SENDER_RECEIVER peer registered.");

  // Register RECEIVER as peer (may be same MAC in 2-board test — ESP-NOW handles this fine)
  // Only add if different from SENDER_RECEIVER to avoid duplicate peer error
  if (!macEqual(RECEIVER_MAC, SENDER_RECEIVER_MAC)) {
    esp_now_peer_info_t peer2 = {};
    memcpy(peer2.peer_addr, RECEIVER_MAC, 6);
    peer2.channel = 0;
    peer2.encrypt = false;
    if (esp_now_add_peer(&peer2) != ESP_OK)
      Serial.println("[ERROR] Failed to add RECEIVER peer.");
    else
      Serial.println("[BOOT] RECEIVER peer registered (3-board mode).");
  } else {
    Serial.println("[BOOT] 2-board mode: RECEIVER == SENDER_RECEIVER (single peer).");
  }

  Serial.println("[BOOT] Relay ready. Waiting for packets...\n");
}

// ─────────────────────────────────────────────
//  MAIN LOOP — nothing to do, callbacks handle everything
// ─────────────────────────────────────────────
void loop()
{
  // Relay is purely event-driven.
  // Periodic heartbeat so you can confirm it's alive.
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {
    Serial.println("[RELAY] Heartbeat — alive, waiting for packets.");
    lastHeartbeat = millis();
  }
}
