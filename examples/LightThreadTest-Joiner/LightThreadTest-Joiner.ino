#include <LightThread.h>

#define JOIN_BUTTON_PIN 9

LightThread thread(JOIN_BUTTON_PIN, Role::JOINER);

void setup() {
  Serial.begin(115200);
  thread.begin();
}

void loop() {
  thread.update();
  delay(10);
}
