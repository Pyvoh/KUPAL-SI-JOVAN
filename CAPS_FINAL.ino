#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "BOSSING!!";        
const char* password = "kupalkababoss"; 

// Web server configuration
const char* serverAddress = "ecobot-server.onrender.com";
const int serverPort = 443;
const char* bottleDataEndpoint = "/api/bottle-data"; 
const char* rewardBottleEndpoint = "/api/reward-bottle";
const char* binStatusEndpoint = "/api/bin-status";

WiFiSSLClient wifiClient;
HttpClient httpClient = HttpClient(wifiClient, serverAddress, serverPort);

// Connection timeout settings
const unsigned long HTTP_TIMEOUT = 15000;
const unsigned long CONNECTION_TIMEOUT = 10000;

// WiFi connection management
bool wifiConnected = false;
bool serverReachable = false;
unsigned long lastWiFiCheck = 0;
unsigned long lastServerCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; 
const unsigned long SERVER_CHECK_INTERVAL = 60000;
const unsigned long WIFI_RECONNECT_DELAY = 5000;
int wifiReconnectAttempts = 0;
const int MAX_WIFI_RECONNECT_ATTEMPTS = 3;

// Hardware communication
SoftwareSerial slaveSerial(12, A1); 

// LCD configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pin definitions
const int BUZZER_PIN = A0;
const int START_BUTTON_PIN = 11;
const int RESET_BUTTON_PIN = 3;
const int IR_PIN = 2;
const int TRIG_PIN = 9;
const int ECHO_PIN = 10;
const int LED_PIN = 13;

// Color sensor pins
#define S0 4
#define S1 5
#define S2 6
#define S3 7
#define sensorOut 8

// Button handling
struct ButtonState {
  bool current = HIGH;
  bool last = HIGH;
  unsigned long lastDebounceTime = 0;
  bool pressed = false;
};

ButtonState startButton;
ButtonState resetButton;
const unsigned long DEBOUNCE_DELAY = 50;

// Color sensor data
struct ColorReading {
  int red = 0;
  int green = 0;
  int blue = 0;
};

ColorReading colorData;

// System state
int bottleCount = 0;
const int TARGET_BOTTLES = 3;
bool taskCompleted = false;
bool systemReady = false;
bool greenBottleDetected = false;
unsigned long greenDetectionTime = 0;
const unsigned long GREEN_BLOCK_DURATION = 3000;

// Level monitoring - OPTIMIZED FOR 10CM THRESHOLD
bool containerFull = false;
bool lastContainerFullState = false;
unsigned long lastLevelCheck = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 200;
const float CONTAINER_HEIGHT = 20.0;
const float FULL_THRESHOLD = 10.0; // 10cm threshold
const int READINGS_TO_AVERAGE = 3;
float distanceReadings[READINGS_TO_AVERAGE];
int readingIndex = 0;
bool binStatusSent = false; // Prevent duplicate sends

// Servo control
enum ServoCommand {
  SERVO_REJECT_CMD = 1,
  SERVO_DISPENSE_CMD = 2,
  SERVO_RESET_CMD = 3,
  SERVO_STATUS_CMD = 4,
  SERVO_COUNTER_INCREMENT_CMD = 5,
  SERVO_EMPTY_CMD = 6
};

bool servoInAction = false;
unsigned long servoCommandTime = 0;
const unsigned long SERVO_ACTION_TIMEOUT = 8000;

// IR sensor state
int lastIRReading = HIGH;
unsigned long lastCountTime = 0;
const unsigned long COUNT_DELAY = 500;

// Status tracking
String colorStatus = "CHECKING";
bool shouldCount = true;
bool isRejecting = false;
bool isDispensing = false;
bool isCompletionDispensing = false;

// Session tracking
unsigned long sessionStartTime = 0;
String sessionId = "";

// Buzzer control
struct BuzzerState {
  bool active = false;
  unsigned long startTime = 0;
  int toneCount = 0;
  unsigned long lastTone = 0;
};

BuzzerState buzzer;
const unsigned long BUZZER_DURATION = 3000;
const unsigned long BUZZER_TONE_INTERVAL = 150;
const int BUZZER_FREQUENCY = 1000;
const int BUZZER_MAX_TONES = 6;

void setup() {
  Serial.begin(9600);
  slaveSerial.begin(9600);
  
  Serial.println("=== ECOBOT Initializing ===");
  
  initializePins();
  initializeColorSensor();
  initializeLCD();
  initializeUltrasonicReadings();
  initializeWiFi();
  
  testConnectivity();
  
  sendServoCommand(SERVO_RESET_CMD);
  
  Serial.println("=== ECOBOT Ready ===");
  Serial.println("Features:");
  Serial.println("- Real-time color detection");
  Serial.println("- Smart bin status (sends at 10cm threshold)");
  Serial.println("- Bottle data + reward system (sends when 3 bottles collected)");
  Serial.println("- Triple API endpoints");
  printWiFiStatus();
  printServerStatus();
}

void loop() {
  handleButtons();
  manageWiFiConnection();
  checkServerConnectivity();
  
  // OPTIMIZED: Check container level with smart data sending
  checkContainerLevelOptimized();
  
  // Main system operations
  if (systemReady && !containerFull) {
    detectColorRealTime();
    handleBottleCounting();
    checkServoStatus();
  }
  
  handleBuzzer();
  updateDisplay();
  
  delay(20); // Minimal main loop delay for responsiveness
}

void detectColorRealTime() {
  readColorSensor();
  analyzeColor();
}

// OPTIMIZED: Smart container level checking with threshold-based data sending
void checkContainerLevelOptimized() {
  if (millis() - lastLevelCheck < LEVEL_CHECK_INTERVAL) return;
  
  float distance = getStableUltrasonicDistance();
  lastContainerFullState = containerFull;
  
  // Apply hysteresis to prevent oscillation
  if (containerFull) {
    containerFull = (distance <= FULL_THRESHOLD + 1.0 && distance > 0);
  } else {
    containerFull = (distance <= FULL_THRESHOLD && distance > 0);
  }
  
  digitalWrite(LED_PIN, containerFull ? HIGH : LOW);
  
  // SMART DATA SENDING: Only send when state changes at 10cm threshold
  if (containerFull && !lastContainerFullState) {
    // Container just became full (crossed 10cm threshold)
    Serial.println("🚨 CONTAINER FULL - 10CM THRESHOLD REACHED");
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println("cm");
    
    sendBinStatus();
    binStatusSent = true;
    
  } else if (!containerFull && lastContainerFullState) {
    // Container just became empty (above 10cm threshold)
    Serial.println("✅ CONTAINER EMPTY - ABOVE 10CM THRESHOLD");
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println("cm");
    
    binStatusSent = false;
  }
  
  // Stop system if container becomes full during operation
  if (systemReady && containerFull) {
    Serial.println("Stopping operation - Container at 10cm threshold");
    pauseSystemForFullContainer();
  }
  
  lastLevelCheck = millis();
}

void testConnectivity() {
  Serial.println("\n=== CONNECTIVITY DIAGNOSTICS ===");
  
  Serial.println("1. Testing basic network connectivity...");
  if (testBasicConnectivity()) {
    Serial.println("✓ Basic network connectivity: OK");
  } else {
    Serial.println("✗ Basic network connectivity: FAILED");
  }
  
  Serial.println("2. Testing DNS resolution...");
  if (testDNSResolution()) {
    Serial.println("✓ DNS resolution: OK");
  } else {
    Serial.println("✗ DNS resolution: FAILED");
  }
  
  Serial.println("3. Testing HTTPS connectivity...");
  if (testHTTPSConnectivity()) {
    Serial.println("✓ HTTPS connectivity: OK");
    serverReachable = true;
  } else {
    Serial.println("✗ HTTPS connectivity: FAILED");
    serverReachable = false;
  }
  
  Serial.println("=== DIAGNOSTICS COMPLETE ===\n");
}

bool testBasicConnectivity() {
  WiFiClient testClient;
  bool connected = testClient.connect(IPAddress(8, 8, 8, 8), 53);
  if (connected) {
    testClient.stop();
    return true;
  }
  return false;
}

bool testDNSResolution() {
  WiFiClient testClient;
  Serial.print("Resolving hostname: ");
  Serial.println(serverAddress);
  
  bool resolved = testClient.connect(serverAddress, 80);
  if (resolved) {
    Serial.println("DNS resolution successful");
    testClient.stop();
    return true;
  } else {
    Serial.println("DNS resolution failed");
    return false;
  }
}

bool testHTTPSConnectivity() {
  Serial.print("Testing HTTPS connection to: ");
  Serial.print(serverAddress);
  Serial.print(":");
  Serial.println(serverPort);
  
  WiFiSSLClient testSSLClient;
  testSSLClient.setTimeout(CONNECTION_TIMEOUT);
  
  bool connected = testSSLClient.connect(serverAddress, serverPort);
  if (connected) {
    Serial.println("HTTPS connection established");
    
    testSSLClient.println("GET / HTTP/1.1");
    testSSLClient.print("Host: ");
    testSSLClient.println(serverAddress);
    testSSLClient.println("Connection: close");
    testSSLClient.println();
    
    unsigned long startTime = millis();
    while (!testSSLClient.available() && (millis() - startTime) < 5000) {
      delay(100);
    }
    
    if (testSSLClient.available()) {
      String response = testSSLClient.readStringUntil('\n');
      Serial.print("Server response: ");
      Serial.println(response);
      testSSLClient.stop();
      return response.indexOf("HTTP") >= 0;
    }
    
    testSSLClient.stop();
  } else {
    Serial.println("HTTPS connection failed");
  }
  
  return false;
}

void checkServerConnectivity() {
  if (millis() - lastServerCheck < SERVER_CHECK_INTERVAL) return;
  
  lastServerCheck = millis();
  
  if (wifiConnected) {
    Serial.println("Checking server connectivity...");
    bool wasReachable = serverReachable;
    serverReachable = testHTTPSConnectivity();
    
    if (serverReachable && !wasReachable) {
      Serial.println("✓ Server connection restored!");
    } else if (!serverReachable && wasReachable) {
      Serial.println("✗ Server connection lost!");
    }
  }
}

bool sendHTTPRequest(String endpoint, String jsonPayload, String requestType = "POST") {
  if (!wifiConnected) {
    Serial.println("Cannot send request - WiFi not connected");
    return false;
  }
  
  if (!serverReachable) {
    Serial.println("Cannot send request - Server not reachable");
    return false;
  }
  
  Serial.println("=== Sending HTTP Request ===");
  Serial.println("Endpoint: " + endpoint);
  Serial.println("Payload: " + jsonPayload);
  
  WiFiSSLClient newSSLClient;
  HttpClient newHttpClient = HttpClient(newSSLClient, serverAddress, serverPort);
  
  newSSLClient.setTimeout(HTTP_TIMEOUT);
  
  newHttpClient.beginRequest();
  
  if (requestType == "POST") {
    newHttpClient.post(endpoint);
  } else {
    newHttpClient.get(endpoint);
  }
  
  newHttpClient.sendHeader("Content-Type", "application/json");
  newHttpClient.sendHeader("Content-Length", jsonPayload.length());
  newHttpClient.sendHeader("User-Agent", "Arduino-EcoBot/1.0");
  newHttpClient.sendHeader("Connection", "close");
  
  if (jsonPayload.length() > 0) {
    newHttpClient.beginBody();
    newHttpClient.print(jsonPayload);
  }
  
  newHttpClient.endRequest();
  
  unsigned long requestStart = millis();
  while (!newHttpClient.available() && (millis() - requestStart) < HTTP_TIMEOUT) {
    delay(100);
  }
  
  int statusCode = newHttpClient.responseStatusCode();
  String response = newHttpClient.responseBody();
  
  Serial.print("HTTP Status: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  
  newHttpClient.stop();
  newSSLClient.stop();
  
  bool success = (statusCode == 200 || statusCode == 201);
  
  if (success) {
    Serial.println("✅ Request sent successfully!");
  } else {
    Serial.println("❌ Request failed");
    
    if (statusCode <= 0) {
      serverReachable = false;
      Serial.println("Marking server as unreachable due to connection error");
    }
  }
  
  Serial.println("=========================\n");
  return success;
}

// Send bin status when 10cm threshold is reached
void sendBinStatus() {
  String jsonPayload = "{";
  jsonPayload += "\"bin\": \"REG456\",";
  jsonPayload += "\"status\": \"full\",";
  jsonPayload += "\"location\": \"2nd Floor Hallway\"";
  jsonPayload += "}";
  
  Serial.println("🚨 10CM THRESHOLD EVENT - Sending bin status");
  sendHTTPRequest(binStatusEndpoint, jsonPayload, "POST");
}

// Send bottle data when 3 bottles are collected
void sendBottleData() {
  String jsonPayload = "{";
  jsonPayload += "\"bottles\": " + String(TARGET_BOTTLES) + ",";
  jsonPayload += "\"type\": \"Arduino Collection\",";
  jsonPayload += "\"location\": \"Bin Location A\"";
  jsonPayload += "}";
  
  Serial.println("📊 BOTTLE DATA - Sending bottle collection data");
  sendHTTPRequest(bottleDataEndpoint, jsonPayload, "POST");
}

// Send reward data when 3 bottles are collected
void sendRewardData() {
  String jsonPayload = "{";
  jsonPayload += "\"userId\": \"USR789\",";
  jsonPayload += "\"bottles\": -" + String(TARGET_BOTTLES) + ",";
  jsonPayload += "\"action\": \"decrease\"";
  jsonPayload += "}";
  
  Serial.println("🎁 REWARD DATA - Sending reward bottle data");
  sendHTTPRequest(rewardBottleEndpoint, jsonPayload, "POST");
}

// Send both bottle data and reward data together
void sendBottleAndRewardData() {
  Serial.println("🚀 SENDING DUAL API CALLS - Bottle Data + Reward Data");
  Serial.println("================================================");
  
  // Send bottle data first
  sendBottleData();
  
  // Small delay between requests to prevent server overload
  delay(500);
  
  // Send reward data second
  sendRewardData();
  
  Serial.println("================================================");
  Serial.println("✅ DUAL API CALLS COMPLETED");
}

void initializePins() {
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  Serial.println("Hardware pins initialized");
}

void initializeColorSensor() {
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);
  
  Serial.println("Color sensor initialized");
}

void initializeLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("WELCOME TO ECOBOT");
  lcd.setCursor(0, 1);
  lcd.print("PRESS TO START");
  
  Serial.println("LCD initialized");
}

void initializeUltrasonicReadings() {
  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = getSingleUltrasonicReading();
    delay(100);
  }
  Serial.println("Ultrasonic sensor calibrated");
}

void initializeWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not found!");
    return;
  }
  
  WiFi.disconnect();
  delay(1000);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 20;
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(1000);
    Serial.print(".");
    attempts++;
    
    if (attempts % 5 == 0) {
      Serial.print(" [");
      Serial.print(getWiFiStatusString());
      Serial.print("] ");
    }
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected successfully!");
    
    delay(5000);
    
    int ipWaitAttempts = 0;
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && ipWaitAttempts < 15) {
      delay(1000);
      Serial.print(".");
      ipWaitAttempts++;
    }
    
    if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      wifiConnected = true;
      Serial.println("\nIP address assigned successfully!");
      printConnectionDetails();
    } else {
      Serial.println("\nWarning: No IP address assigned");
      wifiConnected = false;
    }
  } else {
    wifiConnected = false;
    Serial.println("WiFi connection failed");
    Serial.println("System will continue with limited functionality");
  }
}

void manageWiFiConnection() {
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  
  lastWiFiCheck = millis();
  
  bool hasValidIP = (WiFi.localIP() != IPAddress(0, 0, 0, 0));
  
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    serverReachable = false;
    wifiReconnectAttempts = 0;
    Serial.println("WiFi connection lost!");
  } else if (WiFi.status() == WL_CONNECTED && hasValidIP && !wifiConnected) {
    wifiConnected = true;
    wifiReconnectAttempts = 0;
    Serial.println("WiFi connection restored!");
    printConnectionDetails();
    serverReachable = testHTTPSConnectivity();
  } else if (WiFi.status() == WL_CONNECTED && !hasValidIP) {
    Serial.println("WiFi connected but waiting for IP assignment...");
  }
  
  if (!wifiConnected && wifiReconnectAttempts < MAX_WIFI_RECONNECT_ATTEMPTS) {
    Serial.print("Attempting WiFi reconnection... ");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    int quickAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && quickAttempts < 15) {
      delay(1000);
      quickAttempts++;
    }
    
    wifiReconnectAttempts++;
    
    if (WiFi.status() == WL_CONNECTED) {
      delay(3000);
      if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        wifiConnected = true;
        Serial.println("Reconnected with IP!");
        printConnectionDetails();
        serverReachable = testHTTPSConnectivity();
      } else {
        Serial.println("Reconnected but no IP yet");
      }
    } else {
      Serial.println("Failed");
    }
  }
}

void handleButtons() {
  handleStartButton();
  handleResetButton();
}

void handleStartButton() {
  bool reading = digitalRead(START_BUTTON_PIN);
  
  if (reading != startButton.last) {
    startButton.lastDebounceTime = millis();
  }
  
  if ((millis() - startButton.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != startButton.current) {
      startButton.current = reading;
      
      if (startButton.current == LOW && !taskCompleted && !startButton.pressed) {
        if (containerFull) {
          Serial.println("Cannot start - Container is at 10cm threshold!");
          displayMessage("CONTAINER FULL", "ABOVE 10CM LIMIT");
          delay(2000);
        } else {
          startSystem();
        }
      }
    }
  }
  
  startButton.last = reading;
}

void handleResetButton() {
  bool reading = digitalRead(RESET_BUTTON_PIN);
  
  if (reading != resetButton.last) {
    resetButton.lastDebounceTime = millis();
  }
  
  if ((millis() - resetButton.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != resetButton.current) {
      resetButton.current = reading;
      
      if (resetButton.current == LOW && systemReady) {
        resetSystem();
      }
    }
  }
  
  resetButton.last = reading;
}

void startSystem() {
  startButton.pressed = true;
  systemReady = true;
  bottleCount = 0;
  taskCompleted = false;
  greenBottleDetected = false;
  servoInAction = false;
  sessionStartTime = millis();
  sessionId = generateSessionId();
  
  Serial.println("=== ECOBOT STARTED ===");
  Serial.print("Session ID: ");
  Serial.println(sessionId);
  Serial.println("Real-time color detection: ENABLED");
  Serial.println("Smart bin monitoring: 10cm threshold");
  Serial.println("Bottle collection: 3 bottles target");
  Serial.println("Dual API system: Bottle data + Reward system");
  
  lcd.clear();
}

void resetSystem() {
  Serial.println("=== SYSTEM RESET ===");
  
  systemReady = false;
  taskCompleted = false;
  bottleCount = 0;
  greenBottleDetected = false;
  servoInAction = false;
  isRejecting = false;
  isDispensing = false;
  isCompletionDispensing = false;
  startButton.pressed = false;
  shouldCount = true;
  colorStatus = "CHECKING";
  sessionStartTime = 0;
  sessionId = "";
  binStatusSent = false;
  
  if (buzzer.active) {
    buzzer.active = false;
    digitalWrite(BUZZER_PIN, LOW);
    noTone(BUZZER_PIN);
  }
  
  sendServoCommand(SERVO_EMPTY_CMD);
  
  lcd.clear();
  Serial.println("Container emptied, returning to welcome screen");
}

String generateSessionId() {
  return "ECOBOT_" + String(millis()) + "_" + String(random(1000, 9999));
}

void readColorSensor() {
  digitalWrite(S2, LOW); 
  digitalWrite(S3, LOW);
  colorData.red = pulseIn(sensorOut, LOW, 50000);
  
  digitalWrite(S2, HIGH); 
  digitalWrite(S3, HIGH);
  colorData.green = pulseIn(sensorOut, LOW, 50000);
  
  digitalWrite(S2, LOW); 
  digitalWrite(S3, HIGH);
  colorData.blue = pulseIn(sensorOut, LOW, 50000);
}

void analyzeColor() {
  if (colorData.green < colorData.red && colorData.green < colorData.blue && colorData.green > 0) {
    colorStatus = "GREEN-REJECT";
    shouldCount = false;
    greenBottleDetected = true;
    greenDetectionTime = millis();
    
    Serial.println("🔴 GREEN BOTTLE DETECTED - REJECTING");
    sendServoCommand(SERVO_REJECT_CMD);
    isRejecting = true;
    activateBuzzer();
    
  } else if (colorData.blue < colorData.red && colorData.blue < colorData.green && colorData.blue > 0) {
    colorStatus = "BLUE-ACCEPT";
    updateCountingPermission();
    isRejecting = false;
    
  } else {
    colorStatus = "OTHER-ACCEPT";
    updateCountingPermission();
    isRejecting = false;
  }
  
  if (greenBottleDetected && (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
    greenBottleDetected = false;
  }
}

void updateCountingPermission() {
  if (!greenBottleDetected || (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
    shouldCount = true;
    greenBottleDetected = false;
  } else {
    shouldCount = false;
  }
}

void handleBottleCounting() {
  int currentIRReading = digitalRead(IR_PIN);
  
  if (currentIRReading == LOW && lastIRReading == HIGH && 
      shouldCount && !greenBottleDetected && 
      (millis() - lastCountTime) > COUNT_DELAY) {
    
    bottleCount++;
    lastCountTime = millis();
    
    if (bottleCount >= TARGET_BOTTLES) {
      taskCompleted = true;
      isCompletionDispensing = true;
      
      Serial.print("🎯 TARGET REACHED (");
      Serial.print(TARGET_BOTTLES);
      Serial.println(" bottles) - DISPENSING");
      
      // Send BOTH bottle data and reward data together
      sendBottleAndRewardData();
      sendServoCommand(SERVO_DISPENSE_CMD);
    } else {
      Serial.print("✅ BOTTLE ACCEPTED - Count: ");
      Serial.print(bottleCount);
      Serial.print("/");
      Serial.println(TARGET_BOTTLES);
      
      sendServoCommand(SERVO_COUNTER_INCREMENT_CMD);
    }
    
  } else if (currentIRReading == LOW && lastIRReading == HIGH && 
             (colorStatus == "GREEN-REJECT" || greenBottleDetected)) {
    Serial.println("🚫 GREEN BOTTLE BLOCKED - NOT COUNTED");
  }
  
  lastIRReading = currentIRReading;
}

void pauseSystemForFullContainer() {
  systemReady = false;
  taskCompleted = false;
  servoInAction = false;
  isRejecting = false;
  isDispensing = false;
  isCompletionDispensing = false;
  startButton.pressed = false;
  lcd.clear();
}

float getStableUltrasonicDistance() {
  float newReading = getSingleUltrasonicReading();
  
  if (newReading > 0) {
    distanceReadings[readingIndex] = newReading;
    readingIndex = (readingIndex + 1) % READINGS_TO_AVERAGE;
    
    float sum = 0;
    for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
      sum += distanceReadings[i];
    }
    return sum / READINGS_TO_AVERAGE;
  }
  
  return -1;
}

float getSingleUltrasonicReading() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  
  float distance = (duration * 0.0343) / 2;
  return (distance < 1 || distance > 400) ? -1 : distance;
}

void sendServoCommand(int command) {
  slaveSerial.print(command);
  slaveSerial.println();
  
  servoInAction = true;
  servoCommandTime = millis();
  
  Serial.print("🔧 Servo command sent: ");
  Serial.println(command);
}

void checkServoStatus() {
  if (servoInAction && (millis() - servoCommandTime) > SERVO_ACTION_TIMEOUT) {
    servoInAction = false;
    isRejecting = false;
    isDispensing = false;
    
    if (taskCompleted && isCompletionDispensing) {
      completeTask();
    }
  }
  
  if (systemReady && containerFull) {
    pauseSystemForFullContainer();
  }
}

void completeTask() {
  isCompletionDispensing = false;
  Serial.println("✅ Task completed - Resetting system");
  
  systemReady = false;
  taskCompleted = false;
  bottleCount = 0;
  greenBottleDetected = false;
  startButton.pressed = false;
  sessionStartTime = 0;
  sessionId = "";
  lcd.clear();
}

void updateDisplay() {
  if (containerFull) {
    displayMessage("  10CM REACHED   ", "BIN STATUS SENT ");
    return;
  }
  
  if (!systemReady) {
    displayMessage("WELCOME TO ECOBOT", "PRESS TO START  ");
  } else if (isCompletionDispensing && servoInAction) {
    displayMessage("3 Bottles Done! ", "Reward Sent!    ");
  } else if (taskCompleted && !isCompletionDispensing) {
    displayMessage("Task Complete!  ", "Resetting...    ");
  } else if (isRejecting && servoInAction) {
    displayMessage("GREEN BOTTLE    ", "REJECTED!       ");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Insert 3 bottles");
    
    lcd.setCursor(0, 1);
    lcd.print("Count: ");
    lcd.print(bottleCount);
    lcd.print("/3      "); // Added spaces to clear any remaining characters
  }
}

void displayMessage(const char* line1, const char* line2) {
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void activateBuzzer() {
  if (!buzzer.active) {
    buzzer.active = true;
    buzzer.startTime = millis();
    buzzer.toneCount = 0;
    buzzer.lastTone = millis();
    Serial.println("🔊 Buzzer activated - Bottle rejected!");
  }
}

void handleBuzzer() {
  if (!buzzer.active) return;
  
  unsigned long currentTime = millis();
  
  if (currentTime - buzzer.startTime >= BUZZER_DURATION) {
    digitalWrite(BUZZER_PIN, LOW);
    noTone(BUZZER_PIN);
    buzzer.active = false;
    Serial.println("🔇 Buzzer deactivated");
    return;
  }
  
  if (buzzer.toneCount < BUZZER_MAX_TONES && 
      (currentTime - buzzer.lastTone) >= BUZZER_TONE_INTERVAL) {
    
    if (buzzer.toneCount % 2 == 0) {
      tone(BUZZER_PIN, BUZZER_FREQUENCY);
    } else {
      noTone(BUZZER_PIN);
    }
    
    buzzer.toneCount++;
    buzzer.lastTone = currentTime;
  }
  
  if (buzzer.toneCount >= BUZZER_MAX_TONES) {
    noTone(BUZZER_PIN);
  }
}

String getWiFiStatusString() {
  switch (WiFi.status()) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void printConnectionDetails() {
  Serial.println("Network Details:");
  Serial.print("  SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("  IP: ");
  
  IPAddress ip = WiFi.localIP();
  if (ip == IPAddress(0, 0, 0, 0)) {
    Serial.println("0.0.0.0 (IP not assigned yet)");
    Serial.println("  Retrying IP assignment...");
    
    delay(3000);
    ip = WiFi.localIP();
    Serial.print("  Updated IP: ");
    Serial.println(ip);
    
    if (ip == IPAddress(0, 0, 0, 0)) {
      Serial.println("  Warning: Still no IP address - check DHCP settings");
    }
  } else {
    Serial.println(ip);
  }
  
  Serial.print("  Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("  Subnet: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("  Signal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void printWiFiStatus() {
  Serial.print("WiFi Status: ");
  Serial.println(wifiConnected ? "Connected" : "Disconnected");
  if (wifiConnected) {
    Serial.print("Current IP: ");
    Serial.println(WiFi.localIP());
  }
}

void printServerStatus() {
  Serial.print("Server Status: ");
  Serial.println(serverReachable ? "Reachable" : "Unreachable");
  if (!serverReachable && wifiConnected) {
    Serial.println("Note: WiFi connected but server unreachable");
    Serial.println("Check: 1) Server URL, 2) SSL/HTTPS support, 3) Firewall settings");
  }
}
