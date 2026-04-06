unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Listening on U0RXD (GPIO20)...");
}

void loop() {
  while (Serial.available()) {
    byte b = Serial.read();
    Serial.print(b, HEX);
    Serial.print(" ");
  }

  if (millis() - lastPrint > 2000) {
    Serial.println("\n[ESP32 alive, waiting for UART bytes]");
    lastPrint = millis();
  }
}
