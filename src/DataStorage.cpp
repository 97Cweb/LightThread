#include "LightThread.h"
#include "LightThreadConfig.h"
#include "LightThreadTypes.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>

bool LightThread::loadNetworkConfig() {
    if(!SD.begin()) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "SD card mount failed"
        );

        return false;
    }

    File configFile =
        SD.open("/LightThread/network.json");

    if(!configFile) {
        logLightThread(
            LIGHTTHREAD_LOG_WARN,
            "network.json missing; creating default. Edit it and restart the device"
        );

        createDefaultNetworkConfig();
        return false;
    }

    String json;

    while(configFile.available()) {
        json += static_cast<char>(
            configFile.read()
        );
    }

    configFile.close();

    return parseNetworkJson(json);
}

bool LightThread::parseNetworkJson(
    const String &jsonStr
) {
    StaticJsonDocument<
        LIGHTTHREAD_JSON_CAPACITY
    > doc;

    DeserializationError error =
        deserializeJson(doc, jsonStr);

    if(error) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "JSON parse error: %s",
            error.c_str()
        );

        return false;
    }

    const char *roleText =
        doc["identity"]["role"];

    if(roleText == nullptr) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "Missing identity.role"
        );

        return false;
    }

    String roleString(roleText);
    roleString.toLowerCase();

    if(roleString == "leader") {
        role = Role::LEADER;
    } else if(roleString == "joiner") {
        role = Role::JOINER;
    } else {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "Invalid role: %s",
            roleString.c_str()
        );

        return false;
    }

    JsonObject network = doc["network"];

    configuredChannel =
        network["channel"] |
        LIGHTTHREAD_DEFAULT_CHANNEL;

    JsonVariant panId =
        network["panid"];

    if(panId.is<uint16_t>()) {
        configuredPanId =
            panId.as<uint16_t>();
    } else if(panId.is<const char *>()) {
        configuredPanId =
            static_cast<uint16_t>(
                strtoul(
                    panId.as<const char *>(),
                    nullptr,
                    0
                )
            );
    } else {
        configuredPanId =
            LIGHTTHREAD_DEFAULT_PANID;
    }

    const char* ledText = doc["identity"]["led"] | LIGHTTHREAD_DEFAULT_LED;

    configuredLED = ledText;
    configuredLED.toLowerCase();
    if(configuredLED.length() !=3){
        logLightThread(LIGHTTHREAD_LOG_WARN, "identity.led must contain exactly 3 characters, using %s instead",LIGHTTHREAD_DEFAULT_LED);
        configuredLED = LIGHTTHREAD_DEFAULT_LED;
    }

    JsonObject power = doc["power"];
    const char *powerModeText = power["mode"] | LIGHTTHREAD_DEFAULT_POWER_MODE_TEXT;
    String powerModeString(powerModeText);
    powerModeString.toLowerCase();

    if(powerModeString == "awake"){
        configuredPowerMode = PowerMode::AWAKE;
    }
    else if(powerModeString == "sleepy"){
        configuredPowerMode = PowerMode::SLEEPY;
    }
    else if(powerModeString == "dormant"){
        configuredPowerMode = PowerMode::DORMANT;
    }
    else{
        logLightThread(LIGHTTHREAD_LOG_ERROR, "Invalid power.mode: %s", powerModeString.c_str());
        return false;
    }

    configuredPollIntervalMs = power["poll_interval_ms"] | LIGHTTHREAD_DEFAULT_POLL_INTERVAL_MS;

    configuredChildTimeoutSec = power["child_timeout_seconds"] | LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC;

    configuredDormantWakeAfterSeconds = power["wake_after_seconds"] | LIGHTTHREAD_DEFAULT_DORMANT_WAKE_AFTER_SECONDS;

    if(configuredPollIntervalMs < 10){
        logLightThread(LIGHTTHREAD_LOG_WARN, "power.poll_interval_ms must be at least 10, using 10");
        configuredPollIntervalMs = 10;
    }

    if(configuredChildTimeoutSec == 0){
        logLightThread(LIGHTTHREAD_LOG_WARN, "power.child_timeout_seconds cannot be 0, using %lu",static_cast<unsigned long>(LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC));
        configuredChildTimeoutSec = LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC;
    }

    if(configuredPowerMode == PowerMode::DORMANT && configuredDormantWakeAfterSeconds == 0){
        logLightThread(LIGHTTHREAD_LOG_ERROR, "Dormant mode requires power.wake_after_seconds greater than 0");
        return false;
    }

    const char *configuredPowerModeText =
        "awake";

    switch(configuredPowerMode) {
        case PowerMode::AWAKE:
            configuredPowerModeText = "awake";
            break;

        case PowerMode::SLEEPY:
            configuredPowerModeText = "sleepy";
            break;

        case PowerMode::DORMANT:
            configuredPowerModeText = "dormant";
            break;
    }

    logLightThread(
        LIGHTTHREAD_LOG_INFO,
        "Config role=%s channel=%u panid=0x%04x power_mode=%s",
        roleString.c_str(),
        configuredChannel,
        configuredPanId,
        configuredPowerModeText
    );
    if(configuredPowerMode == PowerMode::DORMANT){
        logLightThread(LIGHTTHREAD_LOG_INFO, "Dormant wake interval=%lus",static_cast<unsigned long>(configuredDormantWakeAfterSeconds));
    }

    return true;
}

void LightThread::createDefaultNetworkConfig() {
    if(!SD.exists("/LightThread")) {
        SD.mkdir("/LightThread");
    }

    StaticJsonDocument<
        LIGHTTHREAD_JSON_CAPACITY
    > doc;

    JsonObject identity =
        doc.createNestedObject("identity");

    identity["role"] = "joiner";
    identity["led"] =
        LIGHTTHREAD_DEFAULT_LED;

    JsonObject network =
        doc.createNestedObject("network");

    network["channel"] =
        LIGHTTHREAD_DEFAULT_CHANNEL;


    network["panid"] =
        LIGHTTHREAD_DEFAULT_PANID;

    JsonObject power = doc.createNestedObject("power");
    power["mode"] = LIGHTTHREAD_DEFAULT_POWER_MODE_TEXT;
    power["poll_interval_ms"] =
        LIGHTTHREAD_DEFAULT_POLL_INTERVAL_MS;

    power["child_timeout_seconds"] =
        LIGHTTHREAD_DEFAULT_CHILD_TIMEOUT_SEC;

    power["wake_after_seconds"] =
        LIGHTTHREAD_DEFAULT_DORMANT_WAKE_AFTER_SECONDS;

    SD.remove("/LightThread/network.json");

    File file = SD.open(
        "/LightThread/network.json",
        FILE_WRITE
    );

    if(!file) {
        logLightThread(
            LIGHTTHREAD_LOG_ERROR,
            "Could not create network.json"
        );

        return;
    }

    serializeJsonPretty(doc, file);
    file.close();
}

void LightThread::clearPersistentState() {
    //clears application side state
    //caller responsible for erasing thread dataset
    leaderIp = IPAddress();
    myIp = IPAddress();
}
