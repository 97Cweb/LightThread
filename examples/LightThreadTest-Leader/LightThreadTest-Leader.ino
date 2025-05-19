#include <LightThread.h>


LightThread lightThread;

void setup() {
    Serial.begin(115200);
    lightThread.begin();
    lightThread.registerUdpReceiveCallback([](const String& srcIp, const std::vector<uint8_t>& payload) {
    String msg;
    for (uint8_t b : payload) msg += (char)b;
    log_i("LEADER TEST: Received payload from %s: %s", srcIp.c_str(), msg.c_str());
});

}

void loop() {
    lightThread.update();
    delay(50);
}
