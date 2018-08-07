// Copyright 2018 Western Digital Corporation or its affiliates
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Print out AT command responses (only for slave/scanner mode)
#define DEBUG_OUTPUT 1

// Whether to set the device in scanner mode (0) or skimmer emulation (1, does no actual skimming)
#define EMULATION_MODE 0

// GPIO pin wired to HC-05 EN pin
#define HC05_PIN_EN 9
// Serial baud rate
#define HC05_BAUD 38400

// Maximum number of devices to scan for
#define MAX_DEVICES 5

// Response line buffer size in bytes 
#define RESPONSE_BUFFER_SIZE 256
// AT command timeout in seconds
#define COMMAND_TIMEOUT 6

enum RESPONSE_TYPE {
  RESPONSE_ERROR,
  RESPONSE_OK,
  RESPONSE_FAIL,
  RESPONSE_OTHER
};

// PWM pins for RGB LED
#define LED_RED 6
#define LED_GREEN 3
#define LED_BLUE 5

// Holds BT addresses received from AT+INQ
typedef struct BTDevice {
  uint16_t nap;
  uint8_t uap;
  uint32_t lap : 24;
  bool candidate;
} BTDevice;

// Buffer to hold line we get back from serial
char responseBuffer[RESPONSE_BUFFER_SIZE];
// Buffer multiple lines for output, to avoid corrupting an ongoing AT command
char lines[MAX_DEVICES + 3][RESPONSE_BUFFER_SIZE];
int lineCount = 0;

// Set the onboard LED color using a given RGB hex value
void setLEDColor(unsigned int rgb) {
  analogWrite(LED_RED, 0xFF - ((rgb>>16)&0xFF));
  analogWrite(LED_GREEN, 0xFF - ((rgb>>8)&0xFF));
  analogWrite(LED_BLUE, 0xFF - (rgb&0xFF));
}

// Blink the onboard LED 5 times a second using a given RGB hex value
void blinkLED(unsigned int rgb, unsigned int seconds) {
  unsigned int blinkTime = 5 * seconds;
  for (unsigned int i = 0; i < blinkTime; i++) {
    setLEDColor(rgb);
    delay(100);
    setLEDColor(0);
    delay(100);
  }
}

// Disable and re-enable pin 34, which cycles out/in to AT mode. This seems to help with resets.
void cyclePin34() {
  digitalWrite(HC05_PIN_EN, LOW);
  delay(500);
  digitalWrite(HC05_PIN_EN, HIGH);
  delay(500);
}

// Soft reset, but do not reset to defaults
void reset() {
  while (true) {
    cyclePin34();
    send("AT+RESET", false);  // Wait for reset to complete before printing response
    delay(1000);
    printLines();
    if (checkResponse() != RESPONSE_OK) { // Sometimes reset fails and spews garbage, so retry until we get "OK"
      continue;
    }
    send("AT+INIT");
    delay(500);
    if (checkResponse() != RESPONSE_OK) { // If SPP fails to init, retry to avoid problems later
      continue;
    }
    break;
  }
}

// Send an AT command, and optionally print the device response immediately (for single-line responses).
// Pass printResponse=false if expecting multiline response, or printing response is not desired
void send(const char* cmd, bool printResponse, bool suppressResponseErrors) {
  Serial.println(cmd);
  readLine();
  if (printResponse) {
    printLines(suppressResponseErrors);
  }
}

void send(const char* cmd) {
  send(cmd, true);
}

void send(const char* cmd, bool printResponse) {
  send(cmd, printResponse, true);
}

// Print to terminal. Since console is shared with UART, messages are to the HC-05, resulting in errors.
// suppressResponseError will consume the error from serial so it won't be interpreted as a legitimate device response.
void print(const char* msg, bool suppressResponseError) {
  #if !EMULATION_MODE  // NOOP if in emulation mode
  Serial.println(msg);
  if (suppressResponseError) {
    readLine(false); // Ignore error by consuming device response
  }
  #endif
}

void print(const char* msg) {
  print(msg, true);
}

// Add a line to the output buffer without printint
void addLine(const char* msg) {
  strcpy(lines[lineCount++], msg);
}

// Print all lines in output buffer
void printLines(bool suppressResponseErrors) {
  for (int i = 0; i < lineCount; i++) {
    print(lines[i], suppressResponseErrors);
  }
  lineCount = 0;
}

void printLines() {
  printLines(true);
}

// Read a line from serial (until CRLF) and, if requested, add it to the output buffer
void readLine(bool isResponse) {
  if (isResponse) {
    responseBuffer[0] = '\0';
  }
  // Wait a max of 10 seconds for the next CRLF
  unsigned long timeout = millis() + 10000;
  char c;
  for (int idx = 0; idx < RESPONSE_BUFFER_SIZE && millis() < timeout;) {
    // This will spin until timeout under certain conditions (e.g. printing responses while reset is incomplete)
    if (!Serial.available()) continue;
    c = Serial.read();
    if (c == '\n') {
      if (isResponse) {
        responseBuffer[idx - 1] = 0; // Remove CRLF
      }
      break;
    } else if (isResponse) {
      responseBuffer[idx++] = c;
    }
  }
  // Print out the device response
  if (DEBUG_OUTPUT && isResponse) {
    if (responseBuffer[0] == '\0') {
      addLine("[Empty line]");
    } else {
      addLine(responseBuffer);
    }
  }
}

void readLine() {
  readLine(true);
}

// Parse the response buffer to determine the response type
RESPONSE_TYPE checkResponse() {
  if (strncmp(responseBuffer, "OK", 2) == 0) return RESPONSE_OK;
  if (strncmp(responseBuffer, "FAIL", 4) == 0) return RESPONSE_FAIL;
  if (strncmp(responseBuffer, "ERROR:(", 7) == 0) return RESPONSE_ERROR;
  return RESPONSE_OTHER;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  // Pull EN high to enter AT CMD mode
  pinMode(HC05_PIN_EN, OUTPUT);
  cyclePin34();
  Serial.begin(HC05_BAUD);
  delay(1000);
  // Flush existing reads just in case. Serial.flush() is not implemented
  while (Serial.available()) {
    Serial.read();
  }
  print("Starting up...");
  reset();
  send("AT+ORGL");  // Restore default
  send("AT+RMAAD"); // Remove all cached device addresses
  send("AT+PSWD=1234");
  #if EMULATION_MODE
    send("AT+NAME=HC-05");
    send("AT+ROLE=0");
    send("AT+CLASS=081F00");  // BT CoD: Uncategorized, Capturing
  #else
    send("AT+CMODE=1"); // Connect any address
    send("AT+ROLE=1");
    send("AT+CLASS=0"); // No filter
    char inqm[15];
    sprintf(inqm, "AT+INQM=0,%i,%i", MAX_DEVICES, COMMAND_TIMEOUT);
    send(inqm);
  #endif
  reset();
}

#if EMULATION_MODE
// Emulate a skimmer by responding to known commands
void handleEmulation() {
  digitalWrite(HC05_PIN_EN, LOW);
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '?':
        Serial.println('1');
        break;
      case 'P':
        Serial.println('M');
        break;
    }
  }
}
#else

// Scan for devices, query hostnames, and attempt to connect and communicate with potential skimmers
void handleScan() {
  char stringBuffer[64];
  int numDevices = 0;
  bool skimmerFound = false;
  lineCount = 0;
  BTDevice devices[MAX_DEVICES];

  send("AT+INQ", false);
  // Returns multiple lines in this format: "+INQ:A1B2:C3:D4E5F,2A010C,7FFF\r\n"
  if (checkResponse() == RESPONSE_ERROR) {
    reset();
    return;
  }
  while (checkResponse() == RESPONSE_OTHER) {
    if (strncmp(responseBuffer, "+INQ:", 5) == 0) { // Sometimes the lines get corrupted, ignore bad responses
      devices[numDevices] = {0};
      strtok(responseBuffer, ":,");  // Skip the "+INQ:" part
      devices[numDevices].nap = strtol(strtok(NULL, ":,"), NULL, 16);
      devices[numDevices].uap = strtol(strtok(NULL, ":,"), NULL, 16);
      devices[numDevices].lap = strtol(strtok(NULL, ":,"), NULL, 16);
      numDevices++;
    }
    readLine();
  }
  printLines();
  send("AT+INQC");  // This may not be necessary, but just in case
  reset();

  // Query hostnames for each device found and mark "HC-05" hostnames as candidate devices
  for (int i = 0; i < numDevices; i++) {
    sprintf(stringBuffer, "AT+RNAME?%04x,%02x,%06lx", devices[i].nap, devices[i].uap, devices[i].lap);
    send(stringBuffer, false);
    if (checkResponse() == RESPONSE_OTHER) {
      strtok(responseBuffer, ":"); // Skip the "+RNAME:" part
      if (strcmp(strtok(NULL, ":"), "HC-05") == 0) {
        devices[i].candidate = true;
      }
      readLine();
    }
    printLines();
    // Responses to AT+RNAME should either be 1 or 2 lines, and always end with OK/FAIL/ERROR. Anything else is screwed up, so reset.
    if (checkResponse() == RESPONSE_OTHER) {
      reset();
      return;
    }
  }

  // Attempt connection and commands
  for (int i = 0; i < numDevices; i++) {
    if (!devices[i].candidate) {
      continue;
    }
    sprintf(stringBuffer, "AT+PAIR=%04x,%02x,%06lx,%i", devices[i].nap, devices[i].uap, devices[i].lap, COMMAND_TIMEOUT);
    send(stringBuffer);
    if (checkResponse() != RESPONSE_OK) { // Pairing failure. Ignore and continue checking.
      continue;
    }
    sprintf(stringBuffer, "AT+LINK=%04x,%02x,%06lx", devices[i].nap, devices[i].uap, devices[i].lap);
    send(stringBuffer, false);
    if (checkResponse() != RESPONSE_OK) {
      printLines();
      continue;
    }
    // If connected, device won't respond with error after echoing the AT+LINK response, so there's nothing to suppress
    if (DEBUG_OUTPUT) {
      printLines(false);
    }
    delay(1000);
    // Test to see if the device responds to known skimmer commands
    send("?", true, false);
    if (responseBuffer[0] != '1') {
      devices[i].candidate = false;
    } else {
      send("P", true, false);
      if (responseBuffer[0] != 'M') {
        devices[i].candidate = false;
      }
    }
    cyclePin34();
    send("AT+DISC", false);
    while (checkResponse() == RESPONSE_OTHER) {
      readLine();
    }
    printLines();
    send("AT+RMAAD");
    if (devices[i].candidate) {
      skimmerFound = true;
      sprintf(stringBuffer, "*** Possible skimmer: %04x:%02x:%06lx", devices[i].nap, devices[i].uap, devices[i].lap);
      print(stringBuffer);
      break;  // Greedy, don't check the rest if one skimmer is found
    }
  }
  if (skimmerFound) {
    // Blink red for 10s
    blinkLED(0xff0000, 10);
  } else {
    print("*** No skimmers detected");
    // Blink green for 10s
    blinkLED(0x00ff00, 10);
  }
  reset();
}
#endif

void loop() {
  // Set LED solid orange while busy
  setLEDColor(0xFF7F00);
  #if EMULATION_MODE
  handleEmulation();
  #else
  handleScan();
  #endif
}
