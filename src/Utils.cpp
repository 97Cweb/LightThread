#include "LightThread.h"

// Converts a buffer to a hex string (e.g. {0xAB, 0x01} → "AB01")
String LightThread::convertBytesToHex(const uint8_t *data, size_t len) {
    String hex;
    for(size_t i = 0; i < len; ++i) {
        hex += LIGHTTHREAD_HEX_CHARS[(data[i] >> 4) & 0xF];
        hex += LIGHTTHREAD_HEX_CHARS[data[i] & 0xF];
    }
    return hex;
}

// Converts hex string back to bytes (e.g. "AB01" → {0xAB, 0x01})
bool LightThread::convertHexToBytes(const String &hexStr, std::vector<uint8_t> &out) {
    out.clear();
    if(hexStr.length() % 2 != 0)
        return false;

    for(size_t i = 0; i < hexStr.length(); i += 2) {
        char high = hexStr[i];
        char low = hexStr[i + 1];
        uint8_t byte = strtol((String("") + high + low).c_str(), nullptr, 16);
        out.push_back(byte);
    }
    return true;
}

std::vector<uint8_t> LightThread::hashToBytes(uint64_t hash) {
    std::vector<uint8_t> bytes;

    for(int i = 7; i >= 0; --i) {
        bytes.push_back((hash >> (i * 8)) & 0xFF);
    }

    return bytes;
}

uint64_t LightThread::bytesToHash(const std::vector<uint8_t> &bytes) {
    uint64_t hash = 0;

    for(size_t i = 0; i < bytes.size() && i < 8; ++i) {
        hash <<= 8;
        hash |= bytes[i];
    }

    return hash;
}

String LightThread::hashToString(uint64_t hash) {
    char buf[LIGHTTHREAD_HASH_STRING_BUFFER_SIZE];
    snprintf(buf, sizeof(buf), LIGHTTHREAD_HASH_FORMAT, hash);
    return String(buf);
}

void LightThread::logLightThread(LightThreadLogLevel level, const char *fmt, ...) {
    char buffer[LIGHTTHREAD_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    switch(level) {
    case LIGHTTHREAD_LOG_VERBOSE:
        log_v("[LightThread] %s", buffer);
        break;
    case LIGHTTHREAD_LOG_INFO:
        log_i("[LightThread] %s", buffer);
        break;
    case LIGHTTHREAD_LOG_WARN:
        log_w("[LightThread] %s", buffer);
        break;
    case LIGHTTHREAD_LOG_ERROR:
        log_e("[LightThread] %s", buffer);
        break;
    }
}
