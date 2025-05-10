#include <LightThread.h>

#define JOIN_BUTTON_PIN 9

LightThread lightThread(JOIN_BUTTON_PIN, Role::LEADER);

void setup() {
    Serial.begin(115200);
    lightThread.begin();
}

void loop() {
    lightThread.update();
    delay(50);
}
