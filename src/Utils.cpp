#include "LightThread.h"

// Converts a buffer to a hex string (e.g. {0xAB, 0x01} → "AB01")
String LightThread::convertBytesToHex(const uint8_t* data, size_t len) {
    String hex;
    const char hexChars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        hex += hexChars[(data[i] >> 4) & 0xF];
        hex += hexChars[data[i] & 0xF];
    }
    return hex;
}

// Converts hex string back to bytes (e.g. "AB01" → {0xAB, 0x01})
bool LightThread::convertHexToBytes(const String& hexStr, std::vector<uint8_t>& out) {
    out.clear();
    if (hexStr.length() % 2 != 0) return false;

    for (size_t i = 0; i < hexStr.length(); i += 2) {
        char high = hexStr[i];
        char low = hexStr[i + 1];
        uint8_t byte = strtol((String("") + high + low).c_str(), nullptr, 16);
        out.push_back(byte);
    }
    return true;
}

String LightThread::getLeaderIp(){
  if(getRole() == Role::LEADER){
    return "";
  }
  else if (getRole() == Role::JOINER){
    return leaderIp;
  }
}
