#include "LightThread.h"
#include "OThreadCLI.h"

// Executes a command via the OpenThread CLI and waits for a specific string to appear in the output.
// Parameters:
//   - command: The CLI command to send (e.g., "dataset commit active").
//   - mustContain: A substring that must be present in the CLI response (e.g., "Done").
//   - out: Optional pointer to capture the full CLI response (multi-line).
//   - timeoutMs: Maximum time to wait for a matching response.
// Returns true if the match string is found in time, false otherwise.
bool LightThread::execAndMatch(const String& command, const String& mustContain, String* out, unsigned long timeoutMs) {
    logLightThread(LT_LOG_INFO,"CLI: %s", command.c_str());
    // Send command to OpenThread CLI
    OThreadCLI.println(command);
    String response;
    // Wait for a response that includes the required substring
    if (!waitForString(response, timeoutMs, mustContain)) {
        logLightThread(LT_LOG_WARN,"Command '%s' timed out", command.c_str());
        return false;
    }
    // Store the full response if the caller requested it
    if (out) *out = response;
    return true;
}

// Handles a single line of CLI output.
// loggs an unclaimed (non-parsed) CLI response.
void LightThread::handleCliLine(const String& line) {
    // Optional: Add routing logic later if needed
    logLightThread(LT_LOG_INFO,"CLI Response (unclaimed): %s", line.c_str());
}

// Waits for CLI output to include a specific match string.
// Collects lines into `responseBuffer`. Returns true if match found.
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

    logLightThread(LT_LOG_WARN,"Timeout while waiting for '%s'", matchStr.c_str());
    return false;
}

// Processes individual characters from the CLI to reconstruct full lines.
// Recognizes UDP lines and multi-line CLI responses.
bool LightThread::processCLIChar(char c, String& multiline, bool& isUDP, String& lineOut) {
    static String buffer = "";
  
    // End-of-line handling
    if (c == '\r' || c == '\n') {
        if (buffer.length() == 0) return false;

        String line = buffer;
        buffer = "";
        
        // Detect UDP message (OpenThread format with port 12345)
        if (line.indexOf("bytes from") != -1 && line.indexOf("12345") != -1) {
            isUDP = true;
            lineOut = line;
            multiline = "";  // Clear multiline buffer
            return true;
        }
        
        // Accumulate multi-line CLI output
        multiline += line + "\n";
        
        // End multi-line output when "Done" is detected
        if (line.indexOf("Done") != -1) {
            isUDP = false;
            lineOut = multiline;
            multiline = "";
            return true;
        }
        return false;
    }

    // Still reading a line — accumulate characters
    buffer += c;
    return false;
}

// Fetches a line of CLI or UDP output from the CLI stream.
// Uses `processCLIChar` to handle parsing logic.
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

            // If a complete line is assembled, return it
            if (processCLIChar(c, multiline, isUDP, lineOut)) {
                return true;  // either UDP or CLI complete line
            }
        }

        delay(5); // Yield to avoid tight loop
    }

    // Fallback: If we accumulated some CLI but didn't finish with "Done"
    if (multiline.length() > 0) {
        lineOut = multiline;
        multiline = "";
        isUDP = false;
        return true;
    }

    return false;
}
