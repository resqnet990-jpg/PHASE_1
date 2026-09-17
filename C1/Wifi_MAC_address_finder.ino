#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_STA);  // This line fixes the all-zero issue
  delay(500);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}

// MAC Address: D4:E9:F4:78:E9:70  //for node MCU 1 (T)

// MAC Address: 1C:C3:AB:A0:6C:C8  //for node MCU 2 (R)