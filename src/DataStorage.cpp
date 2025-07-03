#include "LightThread.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>

// Loads and parses the network configuration from SD card.
// If the file is missing, creates a default config.
// Returns true if config was loaded successfully.
bool LightThread::loadNetworkConfig() {
    if(!SD.begin()) {
        logLightThread(LT_LOG_ERROR, "SD card mount failed");
        return false;
    }

    File configFile = SD.open("/LightThread/network.json");
    if(!configFile) {
        logLightThread(LT_LOG_WARN, "/LightThread/network.json not found. Creating default.");
        createDefaultNetworkConfig();
        return false;
    }

    // Read the file into a string
    String jsonStr;
    while(configFile.available()) {
        jsonStr += (char)configFile.read();
    }
    configFile.close();

    return parseNetworkJson(jsonStr);
}

// Parses the contents of network.json and extracts configuration fields.
// Sets internal role and network parameters. Returns false on error.
bool LightThread::parseNetworkJson(const String &jsonStr) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if(err) {
        logLightThread(LT_LOG_ERROR, "JSON parse error: %s", err.c_str());
        return false;
    }

    // Parse identity → role
    if(!doc.containsKey("identity") || !doc["identity"].containsKey("role")) {
        logLightThread(LT_LOG_ERROR, "Missing 'identity.role' in network.json");
        return false;
    }

    String roleStr = (const char *)doc["identity"]["role"];
    roleStr.toLowerCase();

    // Map string to enum Role
    if(roleStr == "leader") {
        role = Role::LEADER;
        roleLoadedFromConfig = true;
    } else if(roleStr == "joiner") {
        role = Role::JOINER;
        roleLoadedFromConfig = true;
    } else {
        logLightThread(LT_LOG_ERROR, "Invalid role '%s' in network.json", roleStr.c_str());
        return false;
    }

    // Parse network details
    JsonObject network = doc["network"];
    if(!network.containsKey("channel") || !network.containsKey("meshlocalprefix") ||
       !network.containsKey("panid")) {
        logLightThread(LT_LOG_ERROR, "Missing required network keys");
        return false;
    }

    configuredChannel = network["channel"];
    configuredPrefix = (const char *)network["meshlocalprefix"];
    configuredPanid = (const char *)network["panid"];

    logLightThread(LT_LOG_INFO, "Config loaded: role=%s, channel=%d, prefix=%s, panid=%s",
                   roleStr.c_str(), configuredChannel, configuredPrefix.c_str(),
                   configuredPanid.c_str());

    return true;
}

// Creates a default /LightThread/network.json with joiner role and safe defaults.
void LightThread::createDefaultNetworkConfig() {
    if(!SD.exists("/LightThread")) {
        SD.mkdir("/LightThread");
    }

    StaticJsonDocument<512> doc;
    JsonObject identity = doc.createNestedObject("identity");
    identity["role"] = "joiner";

    JsonObject network = doc.createNestedObject("network");
    network["channel"] = 11;
    network["meshlocalprefix"] = "fd00::";
    network["panid"] = "0x1234";

    File file = SD.open("/LightThread/network.json", FILE_WRITE);
    if(!file) {
        logLightThread(LT_LOG_ERROR, "Failed to create default /LightThread/network.json");
        return;
    }

    serializeJsonPretty(doc, file);
    file.close();

    logLightThread(LT_LOG_WARN, "Default /network.json created");
}

// Writes the current leader IP and hashmac to leader.json.
// Used by joiners to store their commissioner.
bool LightThread::saveLeaderInfo(const String &ip, const String &hashmac) {
    if(!SD.begin())
        return false;

    if(!SD.exists("/LightThread")) {
        SD.mkdir("/LightThread");
    }

    StaticJsonDocument<256> doc;
    doc["leader_ip"] = ip;
    doc["leader_hash"] = hashmac;

    File file = SD.open("/LightThread/leader.json", FILE_WRITE);
    if(!file) {
        logLightThread(LT_LOG_ERROR, "Failed to write leader.json");
        return false;
    }

    serializeJsonPretty(doc, file);
    file.close();
    return true;
}

// Reads stored leader info back into out parameters.
// Returns false if not found or parse error.
bool LightThread::loadLeaderInfo(String &outIp, String &outHashmac) {
    File file = SD.open("/LightThread/leader.json");
    if(!file)
        return false;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if(err)
        return false;

    outIp = (const char *)doc["leader_ip"];
    outHashmac = (const char *)doc["hashmac"];
    return true;
}

// Removes all persistent config and joiner/leader tracking files.
// Useful for full reset via long-press or factory wipe.
void LightThread::clearPersistentState() {
    logLightThread(LT_LOG_WARN, "WIPING all stored configuration");

    SD.remove("/LightThread/network.json");
    SD.remove("/LightThread/leader.json");

    createDefaultNetworkConfig(); // recreate fresh network.json
}
