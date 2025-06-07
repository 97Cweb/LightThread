#include "LightThread.h"
#include "OThreadCLI.h"

bool LightThread::execAndMatch(const String& command, const String& mustContain, String* out, unsigned long timeoutMs) {
    log_d("CLI: %s", command.c_str());
    OThreadCLI.println(command);
    String response;
    if (!waitForString(response, timeoutMs, mustContain)) {
        log_w("Command '%s' timed out", command.c_str());
        return false;
    }
    if (out) *out = response;
    return true;
}

void LightThread::handleCliLine(const String& line) {
    // Optional: Add routing logic later if needed
    log_d("CLI Response (unclaimed): %s", line.c_str());
}

bool LightThread::waitForString(String& responseBuffer, unsigned long timeoutMs, const String& matchStr) {
    responseBuffer = "";
    unsigned long start = millis();
    String line;
    bool isUDP = false;

    while (millis() - start < timeoutMs) {
        if (otGetResp(line, isUDP, timeoutMs)) {
            if (!isUDP) {
                responseBuffer += line + "\n";
				log_d("CLI Resp: %s", line.c_str());
                if (line.indexOf(matchStr) != -1) {
                    return true;
                }
            }
        }
    }

    log_w("Timeout while waiting for '%s'", matchStr.c_str());
    return false;
}

bool LightThread::processCLIChar(char c, String& multiline, bool& isUDP, String& lineOut) {
    static String buffer = "";

    if (c == '\r' || c == '\n') {
        if (buffer.length() == 0) return false;

        String line = buffer;
        buffer = "";

        if (line.indexOf("bytes from") != -1 && line.indexOf("12345") != -1) {
            isUDP = true;
            lineOut = line;
            multiline = "";  // Clear multiline buffer
            return true;
        }

        multiline += line + "\n";
        if (line.indexOf("Done") != -1) {
            isUDP = false;
            lineOut = multiline;
            multiline = "";
            return true;
        }
        return false;
    }

    buffer += c;
    return false;
}


bool LightThread::otGetResp(String& lineOut, bool& isUDP, unsigned long timeoutMs) {
    static String buffer = "";
    static String multiline = "";
    static String queuedLine = "";
    static bool queuedIsUDP = false;

    // Priority: Return queued line if present
    if (queuedLine.length()) {
        lineOut = queuedLine;
        isUDP = queuedIsUDP;
        queuedLine = "";
        return true;
    }

    unsigned long start = millis();
    isUDP = false;

    while (millis() - start < timeoutMs) {
        while (OThreadCLI.available()) {
            char c = OThreadCLI.read();

            if (processCLIChar(c, multiline, isUDP, lineOut)) {
                return true;  // either UDP or CLI complete line
            }
        }

        delay(5);
    }

    // Timeout fallback
    if (multiline.length() > 0) {
        lineOut = multiline;
        multiline = "";
        isUDP = false;
        return true;
    }

    return false;
}