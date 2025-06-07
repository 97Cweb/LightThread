#include <LightThread.h>

LightThread lightThread;

void setup() {
  Serial.begin(115200);
  lightThread.begin();
}

void loop() {
  lightThread.update();
  if (lightThread.inState(State::JOINER_PAIRED)) {
      static bool sent = false;

      if (!sent && millis() > 3000) {
          std::vector<uint8_t> payload = { 'h', 'e', 'l', 'l', 'o' };
          bool ok = lightThread.sendUdp(lightThread.getLeaderIp(), true, payload);
          log_i("JOINER TEST: Sent reliable payload: %s", ok ? "OK" : "FAIL");
          sent = true;
      }
  }

  
  
  delay(10);
}
