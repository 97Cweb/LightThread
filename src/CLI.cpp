#include "LightThread.h"
#include "OThreadCLI.h"

void LightThread::readCliSerial() {
    static String multiline = "";

    while(OThreadCLI.available()) {
        String lineOut;
        String srcIp;
        bool isUDP = false;

        char c = OThreadCLI.read();

        if(processCLI(c, multiline, isUDP, lineOut, srcIp)) {
            if(isUDP) {
                handleUdpLine(lineOut, srcIp);
            } else {
                handleCliLine(lineOut);
            }
        }
    }
}


// Handles a single line of CLI output.
// logs an unclaimed (non-parsed) CLI response.
void LightThread::handleCliLine(const String &line) {
    // Optional: Add routing logic later if needed
    logLightThread(LT_LOG_INFO, "CLI Response (unclaimed): %s", line.c_str());
}


// Processes individual characters from the CLI to reconstruct full lines.
// Recognizes UDP lines and multi-line CLI responses.
bool LightThread::processCLI(char c, String &multiline, bool &isUDP, String &lineOut, String &srcIpOut) {
    static String buffer = "";

    // End-of-line handling
    if(c == '\r' || c == '\n') {
        if(buffer.length() == 0)
            return false;

        String line = buffer;
        buffer = "";

        // Detect UDP message (OpenThread format with port 12345)
        int ipIndex = line.indexOf("bytes from ");
        if(ipIndex!= -1 && line.indexOf("12345") != -1) {
            srcIpOut = "";
            int ipStart = ipIndex + 11;
            int ipEnd = line.indexOf(' ', ipStart);
            if(ipEnd != -1){
                srcIpOut = line.substring(ipStart,ipEnd);
            }

            isUDP = true;
            lineOut = line;
            multiline = ""; // Clear multiline buffer
            return true;
        }

        // Accumulate multi-line CLI output
        multiline += line + "\n";

        // End multi-line output when "Done" is detected
        if(line.indexOf("Done") != -1) {
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

