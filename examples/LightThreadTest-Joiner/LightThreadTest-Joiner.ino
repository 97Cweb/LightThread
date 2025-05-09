#include <LightThread.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Define Thread network parameters
const String NETWORK_KEY = "00112233445566778899AABBCCDDEEFF"; // Replace with your key
const String NETWORK_NAME = "TestNetwork";
const int CHANNEL = 11;

#define SD_DETECT_PIN 1

#define JOIN_BUTTON_PIN 9
LightThread lightThread(JOIN_BUTTON_PIN, LightThread::JOINER,"", NETWORK_KEY,NETWORK_NAME, CHANNEL);

void updateSDCard(){
  bool cardInserted = (digitalRead(SD_DETECT_PIN) == LOW);
  if(cardInserted){
    Serial.println("SD card detected!");

    if(!SD.begin()){
      Serial.println("Card mount Failed");
    } else{
      Serial.println("Card mount Success");
    }

    listDir(SD, "/", 3);
  }

}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}


void setup() {
    Serial.begin(115200);
    delay(5000);

    // Initialize the SD detect pin
    pinMode(SD_DETECT_PIN, INPUT_PULLUP);

    lightThread.begin(); // Initialize and attempt to join
    updateSDCard();
}

void loop() {
    
    lightThread.update();
    
    

    delay(50);
}
