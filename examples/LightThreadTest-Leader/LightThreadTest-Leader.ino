#include <LightThread.h>

// Define Thread network parameters
const String LEADER_IP = "fd00:0:0:0::1"; // customise if you plan on being near others
const String NETWORK_KEY = "00112233445566778899AABBCCDDEEFF"; // Replace with your key
const String NETWORK_NAME = "TestNetwork";
const int CHANNEL = 11;

#define JOIN_BUTTON_PIN 9

LightThread lightThread(JOIN_BUTTON_PIN, LightThread::LEADER, LEADER_IP, NETWORK_KEY,NETWORK_NAME, CHANNEL);

void setup() {
    Serial.begin(115200);
    lightThread.begin();
}

void loop() {
    lightThread.update();
    delay(50);
}
