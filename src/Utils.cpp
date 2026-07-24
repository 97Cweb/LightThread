#include "LightThread.h"

std::vector<uint8_t> LightThread::hashToBytes(
    uint64_t hash
) {
    std::vector<uint8_t> bytes;
    bytes.reserve(LIGHTTHREAD_HASH_BYTES);

    for(int i = 7; i >= 0; --i) {
        bytes.push_back(
            static_cast<uint8_t>(
                (hash >> (i * 8)) & 0xff
            )
        );
    }

    return bytes;
}

uint64_t LightThread::bytesToHash(
    const std::vector<uint8_t> &bytes
) {
    uint64_t hash = 0;

    size_t count = min(
        bytes.size(),
        static_cast<size_t>(LIGHTTHREAD_HASH_BYTES)
    );

    for(size_t i = 0; i < count; ++i) {
        hash <<= 8;
        hash |= bytes[i];
    }

    return hash;
}

String LightThread::hashToString(
    uint64_t hash
) {
    char buffer[
        LIGHTTHREAD_HASH_STRING_BUFFER_SIZE
    ];

    snprintf(
        buffer,
        sizeof(buffer),
        LIGHTTHREAD_HASH_FORMAT,
        hash
    );

    return String(buffer);
}

void LightThread::logLightThread(
    LightThreadLogLevel level,
    const char *fmt,
    ...
) {
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
