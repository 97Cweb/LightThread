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

    if(cliBusy){
        pendingCliResponse += line;

        if(pendingCliExpected.length() == 0 || 
            pendingCliResponse.indexOf(pendingCliExpected) != -1){
            cliBusy = false;
            cliDone = true;

            logLightThread(
                LIGHTTHREAD_LOG_INFO,
                "CLI matched '%s' for command: %s",
                pendingCliExpected.c_str(),
                pendingCliCommand.c_str()
            );
        }
        return;
    }
    logLightThread(LIGHTTHREAD_LOG_INFO, "CLI Response (unclaimed): %s", line.c_str());
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

        // Detect UDP message (OpenThread format with port LIGHTTHREAD_UDP_PORT)
        int ipIndex = line.indexOf(LIGHTTHREAD_CLI_UDP_FROM_MARKER);
        if(ipIndex!= -1 && line.indexOf(String(LIGHTTHREAD_UDP_PORT)) != -1) {
            srcIpOut = "";
            int ipStart = ipIndex + strlen(LIGHTTHREAD_CLI_UDP_FROM_MARKER);
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

        // End multi-line output when "Done" (LIGHTTHREAD_CLI_DONE) is detected
        if(line.indexOf(LIGHTTHREAD_CLI_DONE) != -1) {
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


bool LightThread::startCliCommand(const String& command, 
                                    const String& expected,
                                    unsigned long timeoutMs ){
    if (cliBusy){
        return false;
    }
    pendingCliCommand = command;
    pendingCliExpected = expected;
    pendingCliResponse = "";

    cliBusy = true;
    clearCliResult();

    cliCommandStart = millis();
    cliCommandTimeout = timeoutMs;

    logLightThread(LIGHTTHREAD_LOG_INFO, "CLI CMD: %s", command.c_str());
    OThreadCLI.println(command);

    return true;
}

void LightThread::resetCliSteps() {
    cliStepIndex = 0;
    cliStepLastRun = 0;
}

bool LightThread::runCliSteps(const CliStep* steps, uint8_t count) {
    if(cliStepIndex >= count) {
        return true;
    }

    const CliStep& step = steps[cliStepIndex];

    // First consume any completed command result.
    // Important: completed commands have cliBusy == false and cliDone == true.
    if(cliCommandDone() || cliCommandFailed()) {
        String response;
        CliResult result = consumeCliResult(&response);

        if(result == CliResult::Failed) {
            return false;
        }

        if(step.requiredText.length() > 0) {
            if(!responseContainsAny(response, step.requiredText.c_str())) {
                return false; // stay on same step and poll again later
            }
        }

        pendingCliResponse = response; // preserve final useful response
        cliStepIndex++;

        return cliStepIndex >= count;
    }

    // If a command is still running, wait.
    if(cliBusy) {
        return false;
    }

    // Start current step.
    if(step.requiredText.length() > 0) {
        if(millis() - cliStepLastRun < LIGHTTHREAD_CLI_STATE_CHECK_INTERVAL_MS) {
            return false;
        }
    }

    cliStepLastRun = millis();

    String expected = LIGHTTHREAD_CLI_DONE;

    if(step.requiredText.length() > 0) {
        expected = "";
    }

    startCliCommand(step.command,
                    expected,
                    LIGHTTHREAD_CLI_DEFAULT_TIMEOUT_MS);

    return false;
}

LightThread::CliResult LightThread::consumeCliResult(String* response) {
    if(cliCommandFailed()) {
        cliFailed = false;
        cliDone = false;
        return CliResult::Failed;
    }

    if(cliCommandDone()) {
        cliDone = false;

        if(response != nullptr) {
            *response = getCliResponse();
        }

        return CliResult::Done;
    }

    return CliResult::Waiting;
}

void LightThread::updateCliCommand(){
    if(!cliBusy) return;

    if(millis() - cliCommandStart >= cliCommandTimeout){
        cliBusy = false;
        cliFailed = true;

        logLightThread(LIGHTTHREAD_LOG_WARN, "CLI timeout: %s", pendingCliCommand.c_str());
    }
}

bool LightThread::cliCommandDone(){
    return cliDone;
}

bool LightThread::cliCommandFailed(){
    return cliFailed;
}

String LightThread::getCliResponse(){
    return pendingCliResponse;
}

void LightThread::clearCliResult(){
    cliDone = false;
    cliFailed = false;
}

