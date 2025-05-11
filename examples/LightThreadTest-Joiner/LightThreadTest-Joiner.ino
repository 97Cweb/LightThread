#include <LightThread.h>

LightThread lightThread;

void setup() {
  Serial.begin(115200);
  lightThread.begin();
}

void loop() {
  lightThread.update();
  delay(10);
}
