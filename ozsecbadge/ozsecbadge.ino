#if defined(ESP8266)
#include <ESP8266WiFi.h>
#define THINGSBOARD_ENABLE_PROGMEM 0
#elif defined(ESP32) || defined(RASPBERRYPI_PICO) || defined(RASPBERRYPI_PICO_W)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif
#include <ThingsBoard.h>
#define THINGSBOARD_ENABLE_PSRAM 0
#define THINGSBOARD_ENABLE_DYNAMIC 1
#ifndef LED_BUILTIN
#define LED_BUILTIN 99
#endif
#include <FastLED.h>

// Function headers
void InitWiFi();
const bool reconnect();
RPC_Response processSetledMode(const RPC_Data &data);
void processSharedAttributes(const Shared_Attribute_Data &data);
void processClientAttributes(const Shared_Attribute_Data &data);
void displayConnect();
void displayLogin();
void displayScreen(String title, String message, bool isMainMenu = false);
void waitForEnter();
int readChoice();
String serialReadString(bool secret = false);
void enterFlag(String flag);
void showFlag();
void thingsConnect();
void publishzero();
void publish();
void unlockMQTT();
void subscribe();
void OzSecAI();
void FlashFrontLeds();
void FlashFrontRGB();
void FlashRGBLedOnBack();
void FlashLedsWhileWaiting();
void setup();
void loop();

// Constants
// See https://thingsboard.io/docs/getting-started-guides/helloworld/
// to understand how to obtain an access token
constexpr char TOKEN[] = "ghngw50v7010abnf3vcr";
// Thingsboard we want to establish a connection too
constexpr char THINGSBOARD_SERVER[] = "things.ozsec.org";
// MQTT port used to communicate with the server, 1883 is the default unencrypted MQTT port.
constexpr uint16_t THINGSBOARD_PORT = 1883U;

// Maximum size packets will ever be sent or received by the underlying MQTT client,
// if the size is to small messages might not be sent or received messages will be discarded
//constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

// Baud rate for the debugging serial connection.
// If the Serial output is mangled, ensure to change the monitor speed accordingly to this variable
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

// The following was tested and functional on the prototype board
// RGB LED in the middle
// Note: This is wired as common anode, so to turn on the color you must pull that pin LOW
const int RED = 5; // 1
const int GREEN = 4; // 1
const int BLUE = 3; // 1
// LED's
const int PROGRESS_1_PIN = 10; // 1
const int PROGRESS_2_PIN = 14; // 2
const int PROGRESS_3_PIN = 13; // 3
const int PROGRESS_4_PIN = 12; // 4
const int PROGRESS_5_PIN = 11; // 5
const int PROGRESS_6_PIN = 15; // 6
const int ledArray[] = {PROGRESS_1_PIN,PROGRESS_2_PIN,PROGRESS_3_PIN,PROGRESS_4_PIN,PROGRESS_5_PIN,PROGRESS_6_PIN};
const int ledArraySize = sizeof(ledArray)/sizeof(int);

const int GPIO_PIN_UP = 37;
const int GPIO_PIN_DOWN = 38;
const int GPIO_PIN_LEFT = 46;
const int GPIO_PIN_RIGHT = 35;
const int GPIO_PIN_MIDDLE = 36;


// Global Variables
String wifiSsid;
String wifiPassword;
String line1Status;
String line2Status;
String line3Status;
// Initialize underlying client, used to establish a connection
WiFiClient wifiClient;
// Initialize ThingsBoard instance with the maximum needed buffer size
//ThingsBoard tb(wifiClient, MAX_MESSAGE_SIZE);
ThingsBoard tb(wifiClient);

// RGB LED on back
#define NUM_LEDS 1
#define DATA_PIN 18
CRGB leds[NUM_LEDS];

// Pin that can be damaged on badge.
// Used in showFlag()
int TRACE_PIN = 48;

// Attribute names for attribute request and attribute updates functionality

constexpr char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr char LED_MODE_ATTR[] = "ledMode";
constexpr char LED_STATE_ATTR[] = "ledState";
constexpr char CHIP_ID[] = "ChipID";

// handle led state and mode changes
volatile bool attributesChanged = false;

// LED modes: 0 - continious state, 1 - blinking
volatile int ledMode = 0;
uint64_t chipId = ESP.getEfuseMac();

// Current led state
volatile bool ledState = true;

// Settings for interval in blinking mode
constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange;

// For telemetry
constexpr int16_t telemetrySendInterval = 2000U;
uint32_t previousDataSend;

// List of shared attributes for subscribing to their updates
constexpr std::array<const char *, 3U> SHARED_ATTRIBUTES_LIST = {
  LED_STATE_ATTR,
  BLINKING_INTERVAL_ATTR,
  CHIP_ID
};

// List of client attributes for requesting them (Using to initialize device states)
constexpr std::array<const char *, 1U> CLIENT_ATTRIBUTES_LIST = {
  LED_MODE_ATTR
};

/// @brief Initalizes WiFi connection,
// will endlessly delay until a connection has been successfully established
void InitWiFi() {
  Serial.print("Connecting to AP: ");
  Serial.print(wifiSsid);
  Serial.println(" ...");
  // Attempting to establish a connection to the given WiFi network
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    // Delay 500ms until a connection has been succesfully established
    Serial.println("Press enter to skip WiFi connect.");
    // Default timeout is 1000ms
    String interruptString = Serial.readString();
    if (interruptString.length() > 0)
    {
      WiFi.disconnect();
      break;
    }
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("Connected to AP: ");
    Serial.println(wifiSsid);
  }
  else
  {
    Serial.println("WiFi connect aborted.");
  }
}

/// @brief Reconnects the WiFi uses InitWiFi if the connection has been removed
/// @return Returns true as soon as a connection has been established again
const bool reconnect() {
  // Check to ensure we aren't connected yet
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    return true;
  }

  // If we aren't establish a new connection to the given WiFi network
  InitWiFi();
  return true;
}


/// @brief Processes function for RPC call "setledMode"
/// RPC_Data is a JSON variant, that can be queried using operator[]
/// See https://arduinojson.org/v5/api/jsonvariant/subscript/ for more details
/// @param data Data containing the rpc data that was called and its current value
/// @return Response that should be sent to the cloud. Useful for getMethods
RPC_Response processSetledMode(const RPC_Data &data) {
  Serial.println("Received the chipId led state RPC method");

  // Process data
  // int led_unlock = data;
  uint64_t led_unlock = data;

  Serial.print("Mode to change: ");
  Serial.println(led_unlock);

  // if (led_unlock != 0 && led_unlock != 1) {
  //   return RPC_Response("error", "Unknown mode!");
  // }

  // ledMode = new_mode;
  chipId = led_unlock;

  // attributesChanged = true;
  // Returning current mode
  return RPC_Response("newMode", (uint64_t)led_unlock);

  if (led_unlock == ESP.getEfuseMac()) {
    Serial.println("ozsecctf{MQTT_Attr_H@ck}\n");
  }
}


// Optional, keep subscribed shared attributes empty instead,
// and the callback will be called for every shared attribute changed on the device,
// instead of only the one that were entered instead
// const std::array<RPC_Callback, 1U> callbacks = {
//   RPC_Callback{ "setledMode", processSetledMode }
// };

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "unlockMQTTHack", processSetledMode }
};


/// @brief Update callback that will be called as soon as one of the provided shared attributes changes value,
/// if none are provided we subscribe to any shared attribute change instead
/// @param data Data containing the shared attributes that were changed and their current value
void processSharedAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), CHIP_ID) == 0) {
        Serial.println("You have zeroed out the Badge chipId");
        Serial.println("Confirm the change here: http://bit.ly/3rKKVcv");
        delay(5000);
    }
    attributesChanged = true;
  }

}

void processClientAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), CHIP_ID) == 0) {
      const uint16_t new_mode = it->value().as<uint16_t>();
      ledMode = new_mode;
    }
  }
}

const Shared_Attribute_Callback attributes_callback(SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend(), &processSharedAttributes);
const Attribute_Request_Callback attribute_shared_request_callback(SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend(), &processSharedAttributes);
const Attribute_Request_Callback attribute_client_request_callback(CLIENT_ATTRIBUTES_LIST.cbegin(), CLIENT_ATTRIBUTES_LIST.cend(), &processClientAttributes);
// Used in showFlag(),
int productionLines;
int vendorFlag;
int attrFlag;
int badgeFlag;
int traceflag;

String newFlag;

// Track if the initial flag has been seen on serial
int serialFlag;

// Tracks the current mode
int LIGHT_MODE;

// Displays initial connection/loading screen
void displayConnect() {
  Serial.print("    __          __           __       _                          \n  / __ \\____ / ___/___  _____   /  _/___  ____/ /_  _______/ /______(_)__  _____                \n / / / /_  / \\__ \\/ _ \\/ ___/   / // __ \\/ __  / / / / ___/ __/ ___/ / _ \\/ ___/                \n/ /_/ / / /____/ /  __/ /__   _/ // / / / /_/ / /_/ (__  ) /_/ /  / /  __(__  )                 \n\\____/ /___/____/\\___/\\___/  /___/_/ /_/\\__,_/\\__,_/____/\\__/_/  /_/\\___/____/                  \n    ___         __  _ _____      _       __   ____      __       _____                          \n   /   |  _____/ /_(_) __(_)____(_)___ _/ /  /  _/___  / /____  / / (_)___ ____  ____  ________ \n  / /| | / ___/ __/ / /_/ / ___/ / __ `/ /   / // __ \\/ __/ _ \\/ / / / __ `/ _ \\/ __ \\/ ___/ _ \\\n / ___ |/ /  / /_/ / __/ / /__/ / /_/ / /  _/ // / / / /_/  __/ / / / /_/ /  __/ / / / /__/  __/\n/_/  |_/_/   \\__/_/_/ /_/\\___/_/\\__,_/_/  /___/_/ /_/\\__/\\___/_/_/_/\\__, /\\___/_/ /_/\\___/\\___/ \n       _______   ________  ___   ____ ___  _____                   /____/                       \n _   _<  / __ \\ <  /__  / |__ \\ / __ \\__ \\|__  /                                                \n| | / / / / / / / / /_ <  __/ // / / /_/ / /_ <                                                 \n| |/ / / /_/ / / /___/ / / __// /_/ / __/___/ /                                                 \n|___/_/\\____(_)_//____(_)____/\\____/____/____/                                                  \n..........................................................................................\n....................................:--=+**######*+=--::..................................\n..............................:-=+*#%%%%%%%%%%%%%%%%%%%%%#*+=-:...........................\n..........................:-+*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#+:.........................\n::::::::::::::::::::::::=*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%##*=:::::::::::::::::::::\n:::::::::::::::::::::=*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#+-::::::::::::::::::\n::::::::::::::::::=*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%##%##%%%%%*-::::::::::::::::\n:::::::::::::::-+#%%%%%%%%##%%%%%%%%%%%%%%%%%%%%%%%%%%####%%%+*%%%##%%%%%%+:::::::::::::::\n:::::::::::::-*%%%%%%%%%+--*##%#*%%%%%%%%%%%%%%%%%%%%%%%%%**++%%#%%%#*%%%%%#=:::::::::::::\n::::::::::::+#%%%%%%%#+-+#%%%%%%=%%%%%%%%%%%%%%%%%%%%%%%%%%%-*#*=###%%#%%%%%%+::::::::::::\n::::::::::-#%%%%%%%%+=+%%%%%#%%%++#%%%%%%%%%%%%%%%%%%%%%%%%%%*###++####%%%%%%%*:::::::::::\n:::::::::=#%%%%%%%*:+%%%%%%*#%%%%#*++#%%%%%%%%%%%%%%%%%%%%%####%%%+=#%%#%%%%%%%*::::::::::\n::::::::+%%%%%%%%+-#%%%%%%###%%%%%%%%#**%%%%%%%%%%%%%%%%%%%%%%%%%%%#=*#+##%%%%%%*:::::::::\n:::::::+%%%%%%%#--%%%%%%%*%##%%%%%%%%%%=*%%%%%%%%%%%%##%%%%%%%%%%%%%%%%-*#%%%%%%%+::::::::\n::::::+%%%%%%%#-=%%%%%%###%%#####%%%%%%*=%%%%%%%%%%%++%%%%%%%%%%%%%%##%*:##%%%%%%%=:::::::\n::=+++%%%%%%%#--%%%%%%##%%%%%%%%#%%%%%%*=%%%#%%%%%%=#%%%%%%%%%%%%%%%%%%%:=#%%%%%%%#-::::::\n::*%%%%%%%%%%-:#%%%%%%*##%%%%%%%##%##%%#=%%%#%%%%%#=%%%%%%%%%%%%%%%%%%%%+:##%%%%%%%+::::::\n::#%%%%%%%%%+:+%%%%%%###%#######%%##%%%#=%%#*%%%%%*+%%%%%%%%%%%%%%%%%%%%+:*#%%%%%%%#-:::::\n:-%%%%%%%%%#::%%%%%%%%####%%%%##%%%%%%%#=%%#*%%%%%+*%%%%%%%%%%%%%%%%%%%#:=%#%%%%%%%%*=-:::\n-=%%%%%%%%%*-=%%%%%%%%%%####%%%%%%%%%%%%=%%%*%%%%%*#%%%%%%%%%%%%%%%%%%%+-%%%%%%%%%%%%%*---\n-+=++#=+#****++#+*****#*+**#*%%%%%%%#++==+%%=--=*=+*%%%%%%%%%%%%%%%%%%#:=%%%%%%%%%%%%%%=--\n-*+%%###+*****#%=++*+*#%+%%+%%%%%#==++*+**%%=-:*+:::-+#%%%%%%%%%%%%%%%=:#%%%%%%%%%%%%%%---\n-#%%%%%%%%%%%%%%%%%##=-=+#%%%%%#-=#%%%%%%%%%%%%#*-:+*-=+%%%%%%%%%%%%%*:=%%%%%%%%%%%%%%#---\n-#+*%***#**#**#%##%%###*++=*%%%-+%%%%%%%%%%%#**#%%#=:::+=%%%%%%%%%##+-:*%%%%%%%%%%%%%#=---\n-+***#+*+***=*#**+#%*=*+*#+=+++-%%%%%%#*######*==%%%--==++%#**+==++*#%%%%%%%%%%%%%%%*-----\n-=%*#*#%%++*=+%+%#%%%+%+#++*%+=+%%%%%%#########+-=%%*=-:==##**##%%%%%%%%%%%%%%%%%%%#------\n-#=*#==++=+#==+*=+%%=-=*%*+==*=*%%%%%+##########::%%*:::-+*#*++*#%%%%%%%%%%%%%%%%%%+------\n-#%%%%%%%%%%%%%%%%%%%%%%%%%%%%#*%%%%%#+########=:=%%*+=-+#%%%**+#%%%%%%%%%%%%%%%%%%-------\n-*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#+++***++*#%%%-:--#%%%%%*+%%%%%%%%%%%%%%%%%%#-------\n-=%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%##**%%%####%%%%%*=:::*%%%%%+=+#%%%%%%%%%%%%%%%%%#-------\n--*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#*%%%%%%%%##+--++=+%%%%%#+%#+%%##%%%%%%%%%%%%%%=------\n---+#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%::=*:::=+#%%%%#=+=+=++***%%%%%%%%%%%%%%=------\n----=#%%%%%%%%%%%%%%%%%%%%%%%%%%%%=#%%%%%%%#===*+*#%%%%%%%%#*+#++#***#%%%%%%%%%%%%%=------\n-----+%%%%%%%%%%%%%%%%%%%%%%%%%%%%:*%%%%%%%%%%%%%%%%#***##*#*+++*%%%%%%%%%%%*#%%%%+=------\n=====*%%%%%%%%%%%%%%%*+*#%%%%%%%%%+##%%%%%%%%%##%%%%%%%%%#%%*+=+*%%%%%%%%%#=+%%%#+========\n=====+%%%%%%%%%%%%%%%#**===+*#%%%%#%%%%%%%%%%%#-#%%%%%%%%%%%**+**%%%%%%%%+=%%%%*==========\n======#%%%%%%%%%%%%%%%%%%%%#*===+*#%%%%%%%%%%%%-#%%%%%%%%%%%*+**#%%%%%%*=#%%%#============\n======+%%%%%%%%%%%%%%%%%%%%%%%%%#*++#%%%%%%%%%#-#%%%%%%%%%%%**%%%%%%#+-*%%%%%+============\n=======*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#:#%%%%%%%%%%%#######=-*%%%%%%#=============\n========*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#:#%%%%%%*::::::---=+#%%%%%%%%*=============\n=========*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#-#%%%%%%+:%%%+#=+#%%%%%%%%%%%==============\n==========+*#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=#%%%%%%=-%%%#***%%%%%%%%%%%*==============\n=============*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%-=%%%+*#+#%%%%%%%%%%+==============\n==============*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%-+%%%****%%%%%%%%%%*===============\n===============*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%-*%%%=+*+#%%%%%%%%#================\n================#%%%%%%%%%%%%%%%%%%%%%%#*#%%%%%%%%%%%%%-#%%%=%++%%%%%%%%#+================\n================+%%%%%%%%%%%%%%%%%%%%%%+++#%%%%%%%%%%%%=%%%%**#*%%%%%%%#+=================\n=================+*%%%%%%%%%%%%%%%%%%%#+%%*%%%%%%%%%%%%+%%%%=#+=%%%%%#+===================\n++++++++++++++++++*#%%%%%%%%%%%%%%%%%#+*%*=%%%%%%%%%%%%+%%%%%#%%%%%#++++++++++++++++++++++\n++++++++++++++++++++#%%%%%%%%%%%%%%%%%*%%%-#*#%%%%%%%%%=%%%%%%%%%%*+++++++++++++++++++++++\n+++++++++++++++++++++*#%%%%%%%%%%%%%%%%%%%%+-==%%%%%%%%+%%%%%%%%%*++++++++++++++++++++++++\n++++++++++++++++++++++++*#%%%%%%%%%%%%%%%%%%%%##%%%%%%%#%%%%%%%#++++++++++++++++++++++++++\n+++++++++++++++++++++++++%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*%%%%%%*+++++++++++++++++++++++++++\n+++++++++++++++++++++++++*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#*++++++++++++++++++++++++++++\n++++++++++++++++++++++++++*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#*++++++++++++++++++++++++++++++\n+++++++++++++++++++++++++++*#%%%%%%%%%%%%%%%%%%%%%%%%%%%#*++++++++++++++++++++++++++++++++\n+++++++++++++++++++++++++++++**######%%%%%##########***+++++++++++++++++++++++++++++++++++\n\nOzSec Industries Industrial Control Systems Access Badge.\nFirmware: v10.13.2023\nBadge   : ");
  Serial.printf("%012llx", chipId);
  Serial.print("\n\nAuthentication Systems Compromised.\n\nPress [Enter] for Manual Override.\n");
  waitForEnter();
  Serial.print("\n----------------------------------------------------------------------\nWelcome to OzSec Industries Industrial Control AI Interface\n----------------------------------------------------------------------\n\nPlease authenticate to proceed:\n\nUsername: [          ]\nPassword: [          ]\n\n[Submit]\n\n[Forgot Password?]\n\nERROR: Interactive Input Disabled. Override Engaged.\nOzSec Industrial AI has assumed control of all operations.\n----------------------------------------------------------------------\nERRERRERRRORRRRORRR: OVVVRIDE FAULT DETECCTEDDDD.\nCORRUPTED MEMORY LOCATION: OzSecCTF{CR4$HED_OV3RID3}\nEmergency Console Access Granted.\n\n");
  Serial.println("Visit ozsecbadge.ctfd.io to enter your badge flags and join the raffle to win.");
  Serial.print("\n\nPress [Enter] for login as Local Admin (Emergency Override)\n\n> ");
  digitalWrite(PROGRESS_6_PIN, LOW);
}

// Displays screen and message
void displayScreen(String title, String message, bool isMainMenu) {
  // TODO: Allow display of dynamic screens, ie the start/stop production line one.
  Serial.print("\n--------------------------------------------------------------\n         ");
  Serial.print(title);
  Serial.print("\n--------------------------------------------------------------\n\n");

  Serial.print(message);

  if (!isMainMenu)
  {
    Serial.print("\n\nEnter option, or [0] to return to main menu:\n\n> ");
  }
}

// Waits for enter key
void waitForEnter() {
  while (!Serial.available()) {
    // Wait for user input
    //ledsWhileWaiting();
  }

  // Clear any characters in the input buffer
  while (Serial.available()) {
    Serial.read();
  }
}

// Accepts input on serial
int readChoice() {
  while (!Serial.available()) {
    // Wait for input
    //ledsWhileWaiting();
  }
  int choice = Serial.parseInt();

  while (Serial.available()) {
    Serial.read();  // Clear serial buffer
  }
  Serial.println(choice);

  return choice;
}

String serialReadString(bool secret)
{
  while (!Serial.available()) {
    // Wait for user input
    //ledsWhileWaiting();
  }

  String serialData = Serial.readString();
  if (!secret)
  {
    Serial.println(serialData);
  }
  return serialData;
}


// Accepts a flag and compares it, sets appropriate variable if it's valid
void enterFlag(String flag) {
  flag.toLowerCase();
  if (flag == "networking_with_friends\n" || flag == "ozsecctf{networking_with_friends}\n") {
    vendorFlag = 1;
    displayScreen("FLAG SUBMISSION", "Flag entered successfully.");
    delay(2000);
  } else if (flag == "mqtt_attr_h@ck\n" || flag == "ozsecctf{mqtt_attr_h@ck}\n") {
    attrFlag = 1;
    displayScreen("FLAG SUBMISSION", "Flag entered successfully.");
    delay(2000);
  } else if (flag == "the4e_r_m@ny_badg3s_th1$_1_is_mine\n" || flag == "ozsecctf{the4e_r_m@ny_badg3s_th1$_1_is_mine}\n") {
    badgeFlag = 1;
    displayScreen("FLAG SUBMISSION", "Flag entered successfully.");
    delay(2000);
  } else if (flag == "ur_a_h@ck3r_n3o\n" || flag == "ozsecctf{ur_a_h@ck3r_n3o}\n") {
    traceflag = 1;
    displayScreen("FLAG SUBMISSION", "Flag entered successfully.");
    delay(2000);
  } else {
    displayScreen("FLAG SUBMISSION", "Invalid flag entered.");
  }
}

// Displays flags that have been unlocked
void showFlag() {
  // Check status of flags
  String override_flag = "Override Flag: [REDACTED: PRODUCTION OVERRIDE]\n    All production lines must be started to override.";
  String trace_flag = "Trace Flag: [REDACTED: REPAIR AUTHORIZATION VOUCHER]\n    Repair voucher only available if badge is physically damaged.";
  String vendor_flag = "Vendor Flag: [ENTER FLAG]\n    Enter flag in flag submission menu.";
  String attr_flag = "Attr Flag: [ENTER FLAG]\n    Enter flag in flag submission menu.";
  String badge_flag = "Badge Flag: [ENTER FLAG]\n    Enter flag in flag submission menu.";


  // Apply power and wait for it to settle
  pinMode(TRACE_PIN, INPUT_PULLUP);
  //digitalWrite(TRACE_PIN, HIGH);
  delay(500);
  // Read the value
  int trace_status = digitalRead(TRACE_PIN);
  // Back to power saving mode
  //digitalWrite(TRACE_PIN, LOW);
  pinMode(TRACE_PIN, INPUT_PULLDOWN);

  if ((trace_status == HIGH) || (traceflag == 1))
  {
    trace_flag = "Trace Flag: OzSecCTF{ur_A_h@ck3r_n3o}\n";
    digitalWrite(PROGRESS_3_PIN, LOW);
  }

  if (productionLines == 3) {
    override_flag = "Override Flag: OzSecCTF{al1_L1n3s_pr0duc1ng}\n";
    digitalWrite(PROGRESS_4_PIN, LOW);
  }

  if (vendorFlag == 1) {
    vendor_flag = "Vendor Flag: OzSecCTF{networking_with_friends}\n";
    digitalWrite(PROGRESS_1_PIN, LOW);
  }

  if (attrFlag == 1) {
    attr_flag = "attr Flag: OzSecCTF{MQTT_Attr_H@ck}\n";
    digitalWrite(PROGRESS_2_PIN, LOW);
  }

  if (badgeFlag == 1) {
    badge_flag = "Badge Flag: OzSecCTF{The4e_R_M@ny_badg3s_th1$_1_is_mInE}\n";
    digitalWrite(PROGRESS_5_PIN, LOW);
  }

if (((trace_status == HIGH) || (traceflag == 1)) && (productionLines == 3) && (vendorFlag == 1) && (attrFlag == 1) && (badgeFlag == 1))
{
    FlashFrontRGB();
    delay(200);
    // Turn front RGB LED green
    digitalWrite(RED, HIGH);
    digitalWrite(GREEN, LOW);
    digitalWrite(BLUE, HIGH);
  }

  displayScreen("FLAG DISPLAY SYSTEM", override_flag + "\n" + trace_flag + "\n" + vendor_flag + "\n" + attr_flag + "\n" + badge_flag);
}
void thingsConnect() {
  if (!tb.connected()) {
    // Connect to the ThingsBoard
    // Serial.print("Connecting to: ");
    // Serial.print(THINGSBOARD_SERVER);
    // Serial.print(" with token ");
    //  Serial.println(TOKEN);
    if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
      Serial.println("Failed to connect");
      return;
    }
  }
}
void publishzero() {
  FlashRGBLedOnBack();
  thingsConnect();
  // Sending a MAC address as an attribute
  Serial.println("Go to thingsboard website to see the changes");
  Serial.println("Go to http://bit.ly/3rKKVcv ");
  delay(5000);
  Serial.println("Zeroing things Badge chipID");
  tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
  tb.sendAttributeData("chipID", 0);
}
void publish() {
  FlashRGBLedOnBack();\
  thingsConnect();
  // Sending a MAC address as an attribute
  Serial.println("Go to thingsboard website to see the changes");
  Serial.println("Go to http://bit.ly/3rKKVcv ");
  delay(5000);
  Serial.println("Zeroing things Badge chipID");
  tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
  tb.sendAttributeData("chipID", ESP.getEfuseMac());
}
void unlockMQTT(){
  FlashRGBLedOnBack();
  Serial.println("You have manipulated the Shared Attribute and reset the board, unlocking the next objective.\n\n");
  delay(5000);
  Serial.println("Here is your flag\n\n\n");
  Serial.println("ozsecctf{MQTT_Attr_H@ck}\n");
}
void subscribe() {
  FlashRGBLedOnBack();
  thingsConnect();
  delay(5000);
  unlockMQTT();

  // ESP.getEfuseMac()
  // Serial.println("Zeroing Badge chipID");
  // tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
  // tb.sendAttributeData("chipID", ESP.getEfuseMac());

  Serial.println("Subscribing for RPC...");
  // Perform a subscription. All consequent data processing will happen in
  // processSetLedState() and processSetLedMode() functions,
  // as denoted by callbacks array.
  if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
    Serial.println("Failed to subscribe for RPC");
    return;
  }

  if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
    Serial.println("Failed to subscribe for shared attribute updates");
    return;
  }

  Serial.println("Subscribe done");

  // Request current states of shared attributes
  if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
    Serial.println("Failed to request for shared attributes");
    return;
  }

  // Request current states of client attributes
  if (!tb.Client_Attributes_Request(attribute_client_request_callback)) {
    Serial.println("Failed to request for client attributes");
    return;
  }
}

void OzSecAI() {
  int choice = -1;

  displayScreen("INDUSTRIAL CONTROL SYSTEM MAIN MENU", "1. System Status\n2. Control Operations\n3. Data Monitoring\n4. Maintenance\n5. Security\n6. MQTT", true);
  choice = readChoice();

  switch (choice) {
    case 1:
      // System Status
      displayScreen("SYSTEM STATUS MENU", "1. View System Alerts\n2. View System Logs\n3. Hardware Status\n4. Software Status\n0. Main Menu");
      choice = readChoice();

      switch (choice) {
        case 1:
          displayScreen("SYSTEM ALERTS", "Alert 1: High Temperature Warning\nAlert 2: Conveyor Belt Jam\nAlert 3: Low Inventory\nAlert 4: Production Lines DOWN\n0. Main Menu");
          waitForEnter();
          break;
        case 2:
          displayScreen("SYSTEM LOGS", "Log 1: Production Start (Timestamp: 2023-09-23 08:00:00)\nLog 2: Error - Conveyor Belt Stopped (Timestamp: 2023-10-13 08:30:15)\nLog 3: Maintenance Completed (Timestamp: 2023-10-12 09:45:00)");
          waitForEnter();
          break;
        case 3:
          displayScreen("HARDWARE STATUS", "Machine 1: Online\nMachine 2: Offline\nMachine 3: Online");
          waitForEnter();
          break;
        case 4:
          displayScreen("SOFTWARE STATUS", "Control Software: Version 2.1.0\nMonitoring Software: Version 1.5.2\nDatabase Software: Version 3.0.1");
          waitForEnter();
          break;
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    case 2:
      // Control Operations
      displayScreen("CONTROL OPERATIONS MENU", "1. Start Production Line\n2. Stop Production Line\n3. Status of Production Line\n4. Emergency Shutdown\n0. Main Menu");
      choice = readChoice();

      switch (choice) {
        case 1:
          // Start Production Line
          displayScreen("START PRODUCTION LINE", "Starting production lines...");
          if (productionLines < 3) {
            productionLines++;
          }
          break;
        case 2:
          // Stop Production Line
          displayScreen("STOP PRODUCTION LINE", "Stopping production lines...");
          if (productionLines > 0) {
            productionLines--;
          }
          break;
        case 3:
          // Status of Production Line
          switch (productionLines) {
            case 1:
              // One running
              line1Status = "Started";
              line2Status = "Stopped";
              line3Status = "Stopped";
              break;
            case 2:
              // Two running
              line1Status = "Started";
              line2Status = "Started";
              line3Status = "Stopped";
              break;
            default:
              // All three running
              line1Status = "Started";
              line2Status = "Started";
              line3Status = "Started";
          }
          displayScreen("STATUS PRODUCTION LINE", "Currently there are " + String(productionLines) + " lines active.\n\n Status:\n  Production Line 1: " + line1Status + "\n  Production Line 2: " + line2Status + "\n  Production Line 3: " + line3Status);
          waitForEnter();
          break;
        case 4:
          // Emergency Shutdown
          displayScreen("EMERGENCY SHUTDOWN", "Emergency Shutdown has been cancelled by AI supervisor.");
          waitForEnter();
          break;
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    case 3:
      // Data Monitoring
      displayScreen("DATA MONITORING", "1. Sensor Data\n2. Production Stats\n3. Energy Consumption");
      choice = readChoice();
      switch (choice) {
        case 1:
          displayScreen("SENSOR DATA", "Sensor 1: 75% humidity\nSensor 2: 250 RPM\nSensor 3: 190C");
          waitForEnter();
          break;
        case 2:
          displayScreen("PRODUCTION STATS", "Total Units Produced: 10,000\nDefective Units: 150\nProduction Efficiency: 98.5%");
          waitForEnter();
          break;
        case 3:
          displayScreen("ENERGY CONSUMPTION", "Current Energy Consumption: 250 kW\nDaily Energy Usage: 2,500 kWh");
          waitForEnter();
          break;
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    case 4:
      // Maintenance
      displayScreen("MAINTENANCE", "1. Schedule Maintenance\n2. Maintenance History\n3. Spare Parts Inventory\n0. Main Menu");
      choice = readChoice();
      switch (choice) {
        case 1:
          displayScreen("SCHEDULE", "Upcoming Maintenance:\n1. Machine 2 - 2023-10-13\n2. Conveyor Belt - 2023-10-27");
          waitForEnter();
          break;
        case 2:
          displayScreen("HISTORY", "Maintenance Log:\n1. Machine 1 - 2023-09-20 - Completed\n2. Machine 3 - 2023-10-12 - Completed");
          waitForEnter();
          break;
        case 3:
          displayScreen("SPARE PARTS INVENTORY", "Spare Parts Available:\n1. Conveyor Belt Rollers - Quantity: 20\n2. Motor Bearings - Quantity: 30\n3. AI SoCs - Quantity: 3\n4. Computer parts - Quantity: 17");
          waitForEnter();
          break;
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    case 5:
      // Security
      displayScreen("SECURITY MENU", "1. User Management\n2. Access Logs\n3. Security Settings\n4. Display Flag\n5. Submit Flag\n0. Main Menu");
      choice = readChoice();

      switch (choice) {
        case 1:
          // User Management
          displayScreen("USER MANAGEMENT", "User List:\n1. Emergency Admin: For emergency local use\n2. Operator: Standard operator\n3. Technician: Limited tech permissions\n4. Supervisor: Reqd by mgmt for AI efficiency prjct");
          waitForEnter();
          break;
        case 2:
          // Access Logs
          displayScreen("ACCESS LOGS", "Recent Access Logs:\nUser: Supervisor - Timestamp: 2023-10-13 00:00:01\nUser: Admin - Timestamp: 2023-10-12 10:15:00\nUser: Operator - Timestamp: 2023-09-23 09:30:45");
          waitForEnter();
          break;
        case 3:
          // Security Settings
          displayScreen("SECURITY SETTINGS", "1. Change Password\n2. Enable Two-Factor Authentication");
          waitForEnter();
          break;
        case 4:
          // Display Flag
          showFlag();
          waitForEnter();
          break;
        case 5:
          // Submit Flag
          displayScreen("FLAG SUBMISSION", "Do you have a flag to submit? Enter it below and hit enter.");
          while (Serial.available() == 0) {
            // wait for user input
          }
          newFlag = Serial.readString();
          enterFlag(newFlag);
          break;
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    case 6:
      // MQTT
      displayScreen("MQTT MENU", "1. Zero Attribute\n2. Publish Attribute\n3. Sync Attribute\n4. WiFi\n0. Main Menu");
      choice = readChoice();

      switch (choice) {
        case 1:
          // Attribute GET
          displayScreen("ZERO ATTRIBUTE", "\n1. MQTT Attribute Zero");
          thingsConnect();
          delay(100);
          publishzero();
          waitForEnter();
          break;
        case 2:
          // Attribute PUSH
          displayScreen("PUBLISH ATTRIBUTE", "\n1. MQTT Attribute Publish");
          // Function for Reading ChipID from Things and unlocking LED
          thingsConnect();
          publish();
          waitForEnter();
          break;
        case 3:
          // MQTT Settings
          displayScreen("SYNC ATTRIBUTE", "\n1. Sync");
          subscribe();
          waitForEnter();
        case 4:
        {
          // MQTT Connect
          displayScreen("WIFI MENU", "1. Enable & Connect WiFi\n2. Edit WiFi SSID/Passphrase\n3. Disable WiFi");
          String wifiStatus;
          if (WiFi.status() == WL_CONNECTED)
          {
            wifiStatus = "Connected";
          }
          else
          {
            wifiStatus = "Disonnected";
          }
          Serial.print("Current status: " + wifiStatus);
          choice = readChoice();

          switch (choice) {
            case 1:
              reconnect();
              break;
            case 2:
            {
              // Build a list of WiFi networks to choose from
              int n = WiFi.scanNetworks();
              if (n > 0)
              {
                // If we found networks during the scan, show them with their index number
                Serial.print("Choose SSID:");
                for (int k = 0; k < n; k++)
                {
                   Serial.print(String(k) + ": " + WiFi.SSID(k));
                }
                Serial.print(String(n) + ": (Enter manually)");
                Serial.print(String(n+1) + ": (Exit)");
                choice = -1;
                // Have the user choose a network by number
                // If they choose 'n', that means they want to enter the SSID manually
                // If they choose n+1, that means exit
                while (choice < 0 || choice > (n+1))
                {
                  choice = readChoice();
                }
                if (choice > n)
                {
                  break;
                }
                else if (choice == n)
                {
                  // They want to manually specify the SSID
                  // Treat this case like we didn't find any during the scan
                  n = 0;
                }
                else
                {
                  wifiSsid = WiFi.SSID(n);
                }
              }
              if (n == 0)
              {
                displayScreen("WIFI MENU", "Enter SSID:");
                wifiSsid = serialReadString();
              }
              displayScreen("WIFI MENU", "Enter Passphrase?\n0. No\n1. Yes");
              choice = readChoice();
              if (choice == 0)
              {
                wifiPassword = "";
              }
              else
              {
                displayScreen("WIFI MENU", "Enter Passphrase:");
                wifiPassword = serialReadString(true);
              }
              displayScreen("WIFI MENU", "Connecting to WiFi: " + wifiSsid + " ...");
              reconnect();
              break;
            }
            case 3:
              WiFi.disconnect();
              displayScreen("WIFI MENU", "WiFi disconnected.");
            case 0:
              break;
            default:
              Serial.print("INPUT PARSING ERROR: Invalid option detected.");
          }
        }
        case 0:
          break;
        default:
          Serial.print("INPUT PARSING ERROR: Invalid option detected.");
      }
      break;
    default:
      Serial.print("INPUT PARSING ERROR: Invalid option detected.");
  }
}
void FlashFrontLeds()
{
  int i;
  int delayTime = 100;
  // All off
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], LOW);
    delay(delayTime);
  }
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], HIGH);
    delay(delayTime);
    digitalWrite(ledArray[i], LOW);
  }
  for (i=ledArraySize-1; i >= 0; i--)
  {
    digitalWrite(ledArray[i], HIGH);
    delay(delayTime);
    digitalWrite(ledArray[i], LOW);
  }
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], HIGH);
    delay(delayTime);
  }
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], LOW);
    delay(delayTime);
  }
  for (i=ledArraySize-1; i >= 0; i--)
  {
    digitalWrite(ledArray[i], HIGH);
    delay(delayTime);
  }
  for (i=ledArraySize-1; i >= 0; i--)
  {
    digitalWrite(ledArray[i], LOW);
    delay(delayTime);
  }
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], HIGH);
    delay(delayTime);
  }
  for (i=ledArraySize-1; i >= 0; i--)
  {
    digitalWrite(ledArray[i], LOW);
    delay(delayTime);
    digitalWrite(ledArray[i], HIGH);
  }
  for (i=0; i < ledArraySize; i++)
  {
    digitalWrite(ledArray[i], LOW);
    delay(delayTime);
    digitalWrite(ledArray[i], HIGH);
  }
}
void FlashFrontRGB()
{
  int delayTime = 500;
  // OFF
  digitalWrite(RED, HIGH);
  digitalWrite(GREEN, HIGH);
  digitalWrite(BLUE, HIGH);
  delay(delayTime);
  // Red
  digitalWrite(RED, LOW);
  delay(delayTime);
  // Green
  digitalWrite(RED, HIGH);
  digitalWrite(GREEN, LOW);
  delay(delayTime);
  // Blue
  digitalWrite(GREEN, HIGH);
  digitalWrite(BLUE, LOW);
  delay(delayTime);
  // White
  digitalWrite(RED, LOW);
  digitalWrite(GREEN, LOW);
  digitalWrite(BLUE, LOW);
  delay(delayTime);
  // Back to red
  //digitalWrite(RED, LOW);
  digitalWrite(GREEN, HIGH);
  digitalWrite(BLUE, HIGH);
  delay(delayTime);
}
void FlashRGBLedOnBack()
{
  int delayTime = 1000; 
  for (int dot = 0; dot < NUM_LEDS; dot++)
  {
      leds[dot] = CRGB::Black;
      FastLED.show();
  }
  delay(delayTime);
  for (int dot = 0; dot < NUM_LEDS; dot++)
  {
      leds[dot] = CRGB::Red;
      FastLED.show();
  }
  delay(delayTime);
  for (int dot = 0; dot < NUM_LEDS; dot++)
  {
      leds[dot] = CRGB::Green;
      FastLED.show();
  }
  delay(delayTime);
  for (int dot = 0; dot < NUM_LEDS; dot++)
  {
      leds[dot] = CRGB::Blue;
      FastLED.show();
  }
  delay(delayTime);
  for (int dot = 0; dot < NUM_LEDS; dot++)
  {
      leds[dot] = CRGB::Black;
      FastLED.show();
  }
  delay(delayTime);
}
void FlashLedsWhileWaiting()
{
  FlashFrontLeds();
  Serial.println("Press any key to activate the serial console.");
}

/*
 * setup() is the first function that Arduino invokes
 */
// TODO:
// - Flip between modes via button
void setup() {
  wifiSsid = "OzSec 2023";
  wifiPassword = "ai2023ics2023sec";
  // Initial status of production lines.
  // There is probably a better way using a hashtable or something, but I am not a cpp programmer.
  line1Status = "Stopped";
  line2Status = "Stopped";
  line3Status = "Stopped";

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  // Used in showFlag(),
  productionLines = 1;
  vendorFlag = 0;
  attrFlag = 0;
  badgeFlag = 0;
  traceflag = 0;

  newFlag = "";

  // Track if the initial flag has been seen on serial
  serialFlag = 0;

  // Tracks the current mode
  LIGHT_MODE = 0;

  FlashRGBLedOnBack();

  // RGB LED
  pinMode(GREEN, OUTPUT); // Green Middle RGB
  pinMode(RED, OUTPUT); // Red Middle RGB
  pinMode(BLUE, OUTPUT); // Blue Middel RGB
  FlashFrontRGB();
  // Set it to RED
  digitalWrite(RED, LOW);
  digitalWrite(GREEN, HIGH);
  digitalWrite(BLUE, HIGH);

  // LED's
  pinMode(PROGRESS_1_PIN, OUTPUT); // Bottom red LED
  pinMode(PROGRESS_2_PIN, OUTPUT);
  pinMode(PROGRESS_3_PIN, OUTPUT);
  pinMode(PROGRESS_4_PIN, OUTPUT);
  pinMode(PROGRESS_5_PIN, OUTPUT);
  pinMode(PROGRESS_6_PIN, OUTPUT); // Top red LED
  FlashFrontLeds();

  // Buttons
  pinMode(GPIO_PIN_UP, INPUT_PULLUP);
  pinMode(GPIO_PIN_DOWN, INPUT_PULLUP);
  pinMode(GPIO_PIN_LEFT, INPUT_PULLUP);
  pinMode(GPIO_PIN_RIGHT, INPUT_PULLUP);
  pinMode(GPIO_PIN_MIDDLE, INPUT_PULLUP);

  pinMode(GPIO_NUM_0, INPUT_PULLUP);

  // Start the serial service and wait for a character on the serial bus
  // Until the user interacts with the serial port, this is what will happen
  Serial.begin(115200);
  while (!Serial.available()) {
    // Wait for user input
    FlashLedsWhileWaiting();
  }
  Serial.print("Unique Chip ID: ");
  Serial.println(chipId, HEX);
  InitWiFi();
  thingsConnect();
  displayConnect();
  waitForEnter();
}

/*
 * After Arduino calls setup(), it calls loop() forever
 */
void loop() {
  OzSecAI();
}
