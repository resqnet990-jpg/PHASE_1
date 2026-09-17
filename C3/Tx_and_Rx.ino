/*
 * sender_receiver.ino
 * ResQNet — Multi-hop Range & Delay Test
 *
 * Role: SENDER + RECEIVER (combined on one ESP32)
 *
 * Flow:
 *   [SENDER] builds INSTRUCTION packet → sends to RELAY
 *   [RELAY]  forwards to RECEIVER (other board, or this board's RX logic)
 *   [RECEIVER] executes instruction (blink + print) → sends FEEDBACK to RELAY
 *   [RELAY]  forwards FEEDBACK back to SENDER
 *   [SENDER] logs round-trip delay → waits → sends next instruction
 *
 * To move RECEIVER to a third ESP:
 *   1. Copy everything between RECEIVER_LOGIC_BEGIN and RECEIVER_LOGIC_END
 *      into a new .ino file.
 *   2. In that file, register RELAY as the only peer.
 *   3. Remove the RECEIVER section from this file.
 *   4. Done — no other changes needed.
 *
 * Packet format (shared with relay.ino):
 *   [type:1][seq:1][src_mac:6][dst_mac:6][payload:N]
 *   type 0x01 = INSTRUCTION  payload: [cmd:1]
 *   type 0x02 = FEEDBACK     payload: [seq_echo:1][status:1]
 *   cmd  0xA1 = blink + serial print
 *   status 0x00 = OK, 0x01 = FAIL
 */

#include <esp_now.h>
#include <WiFi.h>

// ─────────────────────────────────────────────
//  CONFIGURE MAC ADDRESSES HERE
// ─────────────────────────────────────────────
// Fill these in after reading Serial output on boot.

// MAC of the RELAY node
uint8_t RELAY_MAC[6]    = {0xD4, 0xE9, 0xF4, 0x78, 0xE9, 0x70};

// MAC of THIS board (sender+receiver) — used to build packets.
// This is auto-read from WiFi, but also used as dst in FEEDBACK replies.
// You don't need to change this; it's populated at runtime.
uint8_t MY_MAC[6]       = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// ─────────────────────────────────────────────
//  PACKET CONSTANTS
// ─────────────────────────────────────────────
#define PKT_TYPE_INSTRUCTION  0x01
#define PKT_TYPE_FEEDBACK     0x02
#define CMD_BLINK_PRINT       0xA1
#define STATUS_OK             0x00
#define STATUS_FAIL           0x01

#define HEADER_SIZE           14   // type(1) + seq(1) + src(6) + dst(6)
#define MAX_PKT_SIZE          20   // header + up to 6 bytes payload

// ─────────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────────
volatile bool     feedbackReceived  = false;
volatile uint8_t  lastFeedbackSeq   = 0;
volatile uint8_t  lastFeedbackStatus = STATUS_FAIL;

uint8_t           currentSeq        = 0;
unsigned long     sendTimestamp      = 0;
unsigned long     lastInstructionMs  = 0;

#define INSTRUCTION_INTERVAL_MS  3000   // min gap between instructions
#define FEEDBACK_TIMEOUT_MS      5000   // give up waiting after this

// ─────────────────────────────────────────────
//  PACKET BUILDER
// ─────────────────────────────────────────────
void buildPacket(uint8_t* buf, uint8_t& len,
                 uint8_t type, uint8_t seq,
                 const uint8_t* src, const uint8_t* dst,
                 const uint8_t* payload, uint8_t payloadLen)
{
  buf[0] = type;
  buf[1] = seq;
  memcpy(&buf[2], src, 6);
  memcpy(&buf[8], dst, 6);
  if (payload && payloadLen > 0)
    memcpy(&buf[HEADER_SIZE], payload, payloadLen);
  len = HEADER_SIZE + payloadLen;
}

// ─────────────────────────────────────────────
//  ESP-NOW CALLBACKS
// ─────────────────────────────────────────────
void onSendCallback(const wifi_tx_info_t* mac, esp_now_send_status_t status)
{
  // SENDER: confirm packet left the radio
  Serial.printf("[SENDER][TX] Packet to RELAY — %s\n",
    (status == ESP_NOW_SEND_SUCCESS) ? "Delivered" : "FAILED");
}

void onReceiveCallback(const esp_now_recv_info_t* info,
                       const uint8_t* data, int len)
{
  if (len < HEADER_SIZE) {
    Serial.println("[WARN] Received undersized packet, ignoring.");
    return;
  }

  uint8_t pktType = data[0];
  uint8_t pktSeq  = data[1];
  uint8_t pktSrc[6], pktDst[6];
  memcpy(pktSrc, &data[2], 6);
  memcpy(pktDst, &data[8], 6);

  // ── RECEIVER_LOGIC_BEGIN ─────────────────────────────────────────────
  // Everything between these markers can be cut to a standalone receiver.ino.
  // In standalone mode: replace sendFeedback() target with RELAY_MAC directly.

  if (pktType == PKT_TYPE_INSTRUCTION)
  {
    if (len < HEADER_SIZE + 1) {
      Serial.println("[RECEIVER] Malformed INSTRUCTION, ignoring.");
      return;
    }

    uint8_t cmd = data[HEADER_SIZE];
    Serial.printf("[RECEIVER][RX] INSTRUCTION seq=0x%02X cmd=0x%02X\n", pktSeq, cmd);

    uint8_t execStatus = STATUS_FAIL;

    if (cmd == CMD_BLINK_PRINT)
    {
      // Execute: blink pattern on three LEDs + print
      // ── Customise these values ──
      const int LED_PINS[3]   = {13, 33, 27}; // GPIO pins for LED1, LED2, LED3
      const int BLINK_COUNT   = 1;             // how many times each LED blinks
      const int BLINK_ON_MS   = 500;           // LED on duration (ms)
      const int BLINK_OFF_MS  = 0;           // LED off gap between blinks
      const int LED_GAP_MS    = 0;           // gap between each LED's sequence
      // ───────────────────────────
      Serial.printf("[RECEIVER] Executing: 3 LEDs x %d blinks (%dms on / %dms off)\n",
        BLINK_COUNT, BLINK_ON_MS, BLINK_OFF_MS);
      for (int led = 0; led < 3; led++) {
        Serial.printf("[RECEIVER] LED%d (GPIO%d)\n", led + 1, LED_PINS[led]);
        for (int i = 0; i < BLINK_COUNT; i++) {
          digitalWrite(LED_PINS[led], HIGH); delay(BLINK_ON_MS);
          digitalWrite(LED_PINS[led], LOW);
          if (i < BLINK_COUNT - 1) delay(BLINK_OFF_MS);
        }
        if (led < 2) delay(LED_GAP_MS); // gap between LEDs, not after last one
      }
      Serial.printf("[RECEIVER] Instruction seq=0x%02X executed OK.\n", pktSeq);
      execStatus = STATUS_OK;
    }
    else
    {
      Serial.printf("[RECEIVER] Unknown command 0x%02X — reporting FAIL.\n", cmd);
    }

    // Build and send FEEDBACK back toward RELAY
    // (RELAY will forward it to SENDER)
    uint8_t fbPayload[2] = { pktSeq, execStatus };
    uint8_t fbPkt[MAX_PKT_SIZE];
    uint8_t fbLen = 0;
    buildPacket(fbPkt, fbLen,
                PKT_TYPE_FEEDBACK, pktSeq,
                MY_MAC,      // src = this node
                pktSrc,      // dst = original sender (pktSrc of INSTRUCTION)
                fbPayload, 2);

    esp_err_t err = esp_now_send(RELAY_MAC, fbPkt, fbLen);
    if (err == ESP_OK)
      Serial.printf("[RECEIVER][TX] FEEDBACK seq=0x%02X status=%s sent to RELAY\n",
        pktSeq, (execStatus == STATUS_OK) ? "OK" : "FAIL");
    else
      Serial.printf("[RECEIVER][TX] FEEDBACK send error: %d\n", err);

    return; // don't fall through to SENDER logic
  }

  // ── RECEIVER_LOGIC_END ───────────────────────────────────────────────

  // ── SENDER LOGIC: handle incoming FEEDBACK ───────────────────────────
  if (pktType == PKT_TYPE_FEEDBACK)
  {
    if (len < HEADER_SIZE + 2) {
      Serial.println("[SENDER] Malformed FEEDBACK, ignoring.");
      return;
    }

    uint8_t fbSeq    = data[HEADER_SIZE];
    uint8_t fbStatus = data[HEADER_SIZE + 1];

    unsigned long rtt = millis() - sendTimestamp;

    Serial.println("[SENDER]─────────────────────────────────");
    Serial.printf ("[SENDER][RX] FEEDBACK for seq=0x%02X\n", fbSeq);
    Serial.printf ("[SENDER]     Status       : %s\n",
      (fbStatus == STATUS_OK) ? "OK" : "FAIL");
    Serial.printf ("[SENDER]     Round-trip   : %lu ms\n", rtt);
    Serial.println("[SENDER]─────────────────────────────────");

    if (fbSeq == currentSeq) {
      lastFeedbackSeq    = fbSeq;
      lastFeedbackStatus = fbStatus;
      feedbackReceived   = true;   // unblocks main loop
    } else {
      Serial.printf("[SENDER] Seq mismatch: expected 0x%02X got 0x%02X — ignoring.\n",
        currentSeq, fbSeq);
    }
  }
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  delay(500);

  // Onboard LED
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
  // External LEDs
  pinMode(13, OUTPUT); digitalWrite(13, LOW);
  pinMode(33, OUTPUT); digitalWrite(33, LOW);
  pinMode(27, OUTPUT); digitalWrite(27, LOW);

  // Print this board's MAC
  WiFi.mode(WIFI_STA);
  WiFi.macAddress(MY_MAC);
  Serial.printf("\n[BOOT] This board MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    MY_MAC[0], MY_MAC[1], MY_MAC[2], MY_MAC[3], MY_MAC[4], MY_MAC[5]);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed. Halting.");
    while (true) {}
  }

  esp_now_register_send_cb(onSendCallback);
  esp_now_register_recv_cb(onReceiveCallback);

  // Register RELAY as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RELAY_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ERROR] Failed to add RELAY peer. Check MAC address.");
  } else {
    Serial.println("[BOOT] RELAY peer registered.");
  }

  Serial.println("[BOOT] Ready. Sending first instruction in 2s...");
  delay(2000);
}

// ─────────────────────────────────────────────
//  SENDER: build and send one INSTRUCTION
// ─────────────────────────────────────────────
void sendInstruction()
{
  currentSeq++;
  feedbackReceived = false;

  uint8_t payload[1] = { CMD_BLINK_PRINT };
  uint8_t pkt[MAX_PKT_SIZE];
  uint8_t pktLen = 0;

  // dst = MY_MAC (this board is also the receiver — relay knows to loop back)
  buildPacket(pkt, pktLen,
              PKT_TYPE_INSTRUCTION, currentSeq,
              MY_MAC, MY_MAC,
              payload, 1);

  sendTimestamp = millis();
  Serial.printf("\n[SENDER][TX] INSTRUCTION seq=0x%02X → RELAY\n", currentSeq);

  esp_err_t err = esp_now_send(RELAY_MAC, pkt, pktLen);
  if (err != ESP_OK)
    Serial.printf("[SENDER] esp_now_send error: %d\n", err);
}

// ─────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────
void loop()
{
  unsigned long now = millis();

  // Send first instruction / next instruction after feedback received
  bool timeToSend = (currentSeq == 0) ||
                    (feedbackReceived &&
                     (now - lastInstructionMs >= INSTRUCTION_INTERVAL_MS));

  if (timeToSend) {
    lastInstructionMs = now;
    sendInstruction();
  }

  // Timeout: if no feedback after FEEDBACK_TIMEOUT_MS, move on
  if (!feedbackReceived && currentSeq > 0 &&
      (now - sendTimestamp > FEEDBACK_TIMEOUT_MS))
  {
    Serial.printf("[SENDER] Timeout waiting for feedback on seq=0x%02X. Retrying...\n",
      currentSeq);
    currentSeq--;             // retry same seq
    feedbackReceived = true;  // trick the gate to resend
    lastInstructionMs = now - INSTRUCTION_INTERVAL_MS; // send immediately
  }
}
