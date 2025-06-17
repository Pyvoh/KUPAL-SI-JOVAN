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
const char* serverAddress = "https://v0-arduino-web-app-api.vercel.app"; 
const int serverPort = 443;
const char* bottleDataEndpoint = "/api/bottle-data"; 

WiFiSSLClient wifiClient;  // Use SSL client for HTTPS
HttpClient httpClient = HttpClient(wifiClient, serverAddress, serverPort);

// WiFi connection management
bool wifiConnected = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; 
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
unsigned long lastColorScanTime = 0;
const unsigned long COLOR_SCAN_DELAY = 2000;

// System state
int bottleCount = 0;
const int TARGET_BOTTLES = 3;
bool taskCompleted = false;
bool systemReady = false;
bool greenBottleDetected = false;
unsigned long greenDetectionTime = 0;
const unsigned long GREEN_BLOCK_DURATION = 3000;

// Level monitoring
bool containerFull = false;
unsigned long lastLevelCheck = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 500;
const float CONTAINER_HEIGHT = 20.0;
const float FULL_THRESHOLD = 10.0;
const int READINGS_TO_AVERAGE = 3;
float distanceReadings[READINGS_TO_AVERAGE];
int readingIndex = 0;

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
const unsigned long COUNT_DELAY = 1000;

// Status tracking
String colorStatus = "CHECKING";
bool shouldCount = true;
bool isRejecting = false;
bool isDispensing = false;
bool isCompletionDispensing = false;

// Session tracking
unsigned long sessionStartTime = 0;

// Buzzer control
struct BuzzerState {
  bool active = false;
  unsigned long startTime = 0;
  int toneCount = 0;
  unsigned long lastTone = 0;
};

BuzzerState buzzer;
const unsigned long BUZZER_DURATION = 5000;
const unsigned long BUZZER_TONE_INTERVAL = 200;
const int BUZZER_FREQUENCY = 1000;
const int BUZZER_MAX_TONES = 5;

// Time tracking for ISO date formatting
unsigned long systemStartTime = 0;

void setup() {
  Serial.begin(9600);
  slaveSerial.begin(9600);
  
  Serial.println("=== ECOBOT Initializing ===");
  
  // Record system start time for date calculations
  systemStartTime = millis();
  
  initializePins();
  initializeColorSensor();
  initializeLCD();
  initializeUltrasonicReadings();
  initializeWiFi();
  
  // Reset servo to default position
  sendServoCommand(SERVO_RESET_CMD);
  
  Serial.println("=== ECOBOT Ready ===");
  Serial.println("Features:");
  Serial.println("- 3-bottle target mode");
  Serial.println("- Color sorting (Green reject, Blue/Other accept)");
  Serial.println("- Container level monitoring");
  Serial.println("- WiFi bottle data logging");
  Serial.println("- Reset functionality");
  printWiFiStatus();
}

void loop() {
  handleButtons();
  manageWiFiConnection();
  
  // Container level check runs continuously
  checkContainerLevel();
  
  // Main system operations only when ready and container not full
  if (systemReady && !containerFull) {
    detectColor();
    handleBottleCounting();
    checkServoStatus();
  }
  
  handleBuzzer();
  updateDisplay();
  
  delay(50); // Main loop delay
}

// New function: Get formatted ISO date string
String getFormattedISODate() {
  // Calculate elapsed time since system start
  unsigned long currentTime = millis();
  unsigned long elapsedSeconds = (currentTime - systemStartTime) / 1000;
  
  // Base date: January 1, 2024 (you can adjust this)
  // This is a simplified approach since Arduino doesn't have real-time clock
  unsigned long baseYear = 2024;
  unsigned long baseMonth = 1;
  unsigned long baseDay = 1;
  
  // Calculate current date (simplified - doesn't account for leap years perfectly)
  unsigned long totalDays = elapsedSeconds / 86400; // 86400 seconds in a day
  unsigned long currentSeconds = elapsedSeconds % 86400;
  
  unsigned long currentHour = currentSeconds / 3600;
  unsigned long currentMinute = (currentSeconds % 3600) / 60;
  unsigned long currentSecond = currentSeconds % 60;
  
  // Simple day calculation (assumes 30 days per month for simplicity)
  unsigned long currentYear = baseYear;
  unsigned long currentMonth = baseMonth;
  unsigned long currentDay = baseDay + totalDays;
  
  // Adjust for months and years (simplified)
  while (currentDay > 30) {
    currentDay -= 30;
    currentMonth++;
    if (currentMonth > 12) {
      currentMonth = 1;
      currentYear++;
    }
  }
  
  // Format as ISO 8601 string: YYYY-MM-DDTHH:MM:SSZ
  String isoDate = "";
  isoDate += String(currentYear) + "-";
  
  if (currentMonth < 10) isoDate += "0";
  isoDate += String(currentMonth) + "-";
  
  if (currentDay < 10) isoDate += "0";
  isoDate += String(currentDay) + "T";
  
  if (currentHour < 10) isoDate += "0";
  isoDate += String(currentHour) + ":";
  
  if (currentMinute < 10) isoDate += "0";
  isoDate += String(currentMinute) + ":";
  
  if (currentSecond < 10) isoDate += "0";
  isoDate += String(currentSecond) + ":";
  
  return isoDate;
}

// Updated function: Send bin status with ISO date
void sendBinStatus(String status, String message, float level) {
  // Create JSON string with dynamic timestamp
  String jsonPayload = "{";
  jsonPayload += "\"status\": \"" + status + "\",";
  jsonPayload += "\"message\": \"" + message + "\",";
  jsonPayload += "\"device_id\": \"ecobot_001\",";
  jsonPayload += "\"timestamp\": \"" + getFormattedISODate() + "\",";
  jsonPayload += "}";
  
  Serial.println("Sending bin status:");
  Serial.println(jsonPayload);
  
  if (!wifiConnected) {
    Serial.println("Cannot send bin status - WiFi not connected");
    return;
  }
  
  // Send HTTP request
  httpClient.beginRequest();
  httpClient.post("/api/bin-status"); // Assuming this endpoint exists
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", jsonPayload.length());
  httpClient.beginBody();
  httpClient.print(jsonPayload);
  httpClient.endRequest();
  
  // Handle response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();
  
  Serial.print("Bin Status HTTP Response: ");
  Serial.println(statusCode);
  
  if (statusCode == 200 || statusCode == 201) {
    Serial.println("Bin status sent successfully!");
  } else {
    Serial.println("Failed to send bin status");
  }
}

void initializePins() {
  // Button pins
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  // Sensor pins
  pinMode(IR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Buzzer
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
  
  // Set frequency scaling to 20%
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
  
  // Check for WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not found!");
    return;
  }
  
  // Disconnect any existing connection
  WiFi.disconnect();
  delay(1000);
  
  // Begin connection
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
    
    // Add delay to allow DHCP to complete
    Serial.println("Waiting for IP assignment...");
    delay(3000);
    
    // Check if IP is still 0.0.0.0 and wait
    int ipWaitAttempts = 0;
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && ipWaitAttempts < 10) {
      delay(1000);
      Serial.print(".");
      ipWaitAttempts++;
    }
    
    if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      wifiConnected = true;
      Serial.println("\nIP address assigned successfully!");
      printConnectionDetails();
    } else {
      Serial.println("\nWarning: No IP address assigned, but WiFi connected");
      wifiConnected = true; // Still mark as connected for basic functionality
      printConnectionDetails();
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
  
  // Check both connection status AND IP assignment
  bool hasValidIP = (WiFi.localIP() != IPAddress(0, 0, 0, 0));
  
  // Check connection status
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    // Connection lost
    wifiConnected = false;
    wifiReconnectAttempts = 0;
    Serial.println("WiFi connection lost!");
  } else if (WiFi.status() == WL_CONNECTED && hasValidIP && !wifiConnected) {
    // Connection restored with valid IP
    wifiConnected = true;
    wifiReconnectAttempts = 0;
    Serial.println("WiFi connection restored!");
    printConnectionDetails();
  } else if (WiFi.status() == WL_CONNECTED && !hasValidIP) {
    Serial.println("WiFi connected but waiting for IP assignment...");
  }
  
  // Attempt reconnection if disconnected
  if (!wifiConnected && wifiReconnectAttempts < MAX_WIFI_RECONNECT_ATTEMPTS) {
    Serial.print("Attempting WiFi reconnection... ");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    // Quick connection attempt
    int quickAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && quickAttempts < 10) {
      delay(1000);
      quickAttempts++;
    }
    
    wifiReconnectAttempts++;
    
    if (WiFi.status() == WL_CONNECTED) {
      // Wait for IP assignment
      delay(2000);
      if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        wifiConnected = true;
        Serial.println("Reconnected with IP!");
        printConnectionDetails();
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
          Serial.println("Cannot start - Container is full!");
          displayMessage("CONTAINER FULL", "PLEASE EMPTY");
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
  lastColorScanTime = millis();
  sessionStartTime = millis(); // Record session start time
  
  Serial.println("=== ECOBOT STARTED - 3 BOTTLES MODE ===");
  Serial.print("Session started at: ");
  Serial.println(sessionStartTime);
  
  // Send bin status when system starts
  float currentLevel = getStableUltrasonicDistance();
  sendBinStatus("ACTIVE", "System started - ready for bottles", currentLevel);
  
  lcd.clear();
}

void resetSystem() {
  Serial.println("=== SYSTEM RESET ===");
  
  // Reset all system variables
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
  
  // Stop buzzer
  if (buzzer.active) {
    buzzer.active = false;
    digitalWrite(BUZZER_PIN, LOW);
    noTone(BUZZER_PIN);
  }
  
  // Send bin status when system resets
  float currentLevel = getStableUltrasonicDistance();
  sendBinStatus("RESET", "System reset - container emptied", currentLevel);
  
  // Empty container
  sendServoCommand(SERVO_EMPTY_CMD);
  
  lcd.clear();
  Serial.println("Container emptied, returning to welcome screen");
}

void detectColor() {
  if (millis() - lastColorScanTime < COLOR_SCAN_DELAY) return;
  
  lastColorScanTime = millis();
  
  readColorSensor();
  analyzeColor();
}

void readColorSensor() {
  // Read RED
  digitalWrite(S2, LOW); 
  digitalWrite(S3, LOW);
  colorData.red = pulseIn(sensorOut, LOW, 50000);
  
  // Read GREEN
  digitalWrite(S2, HIGH); 
  digitalWrite(S3, HIGH);
  colorData.green = pulseIn(sensorOut, LOW, 50000);
  
  // Read BLUE
  digitalWrite(S2, LOW); 
  digitalWrite(S3, HIGH);
  colorData.blue = pulseIn(sensorOut, LOW, 50000);
  
  Serial.print("Color readings - R:");
  Serial.print(colorData.red);
  Serial.print(" G:");
  Serial.print(colorData.green);
  Serial.print(" B:");
  Serial.println(colorData.blue);
}

void analyzeColor() {
  if (colorData.green < colorData.red && colorData.green < colorData.blue && colorData.green > 0) {
    // Green bottle detected - reject
    colorStatus = "GREEN-REJECT";
    shouldCount = false;
    greenBottleDetected = true;
    greenDetectionTime = millis();
    
    Serial.println("GREEN BOTTLE DETECTED - REJECTING");
    sendServoCommand(SERVO_REJECT_CMD);
    isRejecting = true;
    activateBuzzer();
    
    // Send bin status for rejected bottle
    float currentLevel = getStableUltrasonicDistance();
    sendBinStatus("REJECT", "Green bottle rejected", currentLevel);
    
  } else if (colorData.blue < colorData.red && colorData.blue < colorData.green && colorData.blue > 0) {
    // Blue bottle - accept
    colorStatus = "BLUE-ACCEPT";
    updateCountingPermission();
    isRejecting = false;
    
  } else {
    // Other color - accept
    colorStatus = "OTHER-ACCEPT";
    updateCountingPermission();
    isRejecting = false;
  }
  
  // Clear green detection after timeout
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
  
  // Detect bottle passage
  if (currentIRReading == LOW && lastIRReading == HIGH && 
      shouldCount && !greenBottleDetected && 
      (millis() - lastCountTime) > COUNT_DELAY) {
    
    bottleCount++;
    lastCountTime = millis();
    
    if (bottleCount >= TARGET_BOTTLES) {
      // Target reached - send bottle data
      taskCompleted = true;
      isCompletionDispensing = true;
      
      Serial.print("TARGET REACHED (");
      Serial.print(TARGET_BOTTLES);
      Serial.println(" bottles) - DISPENSING");
      
      // Send bottle data to API
      sendBottleData();
      
      // Send bin status for completion
      float currentLevel = getStableUltrasonicDistance();
      sendBinStatus("COMPLETE", "Target bottles collected - dispensing reward", currentLevel);
      
      sendServoCommand(SERVO_DISPENSE_CMD);
    } else {
      // Increment counter
      Serial.print("BOTTLE ACCEPTED - Count: ");
      Serial.print(bottleCount);
      Serial.print("/");
      Serial.println(TARGET_BOTTLES);
      
      // Send bin status for accepted bottle
      float currentLevel = getStableUltrasonicDistance();
      sendBinStatus("ACCEPT", "Bottle " + String(bottleCount) + " accepted", currentLevel);
      
      sendServoCommand(SERVO_COUNTER_INCREMENT_CMD);
    }
    
  } else if (currentIRReading == LOW && lastIRReading == HIGH && 
             (colorStatus == "GREEN-REJECT" || greenBottleDetected)) {
    Serial.println("GREEN BOTTLE BLOCKED - NOT COUNTED");
  }
  
  lastIRReading = currentIRReading;
}

void checkContainerLevel() {
  if (millis() - lastLevelCheck < LEVEL_CHECK_INTERVAL) return;
  
  float distance = getStableUltrasonicDistance();
  bool wasFull = containerFull;
  
  // Apply hysteresis to prevent oscillation
  if (containerFull) {
    containerFull = (distance <= FULL_THRESHOLD + 1.0 && distance > 0);
  } else {
    containerFull = (distance <= FULL_THRESHOLD && distance > 0);
  }
  
  digitalWrite(LED_PIN, containerFull ? HIGH : LOW);
  
  // Handle status changes
  if (containerFull && !wasFull) {
    Serial.println("CONTAINER FULL");
    sendBinStatus("FULL", "Container is full - please empty", distance);
  } else if (!containerFull && wasFull) {
    Serial.println("CONTAINER EMPTY");
    sendBinStatus("EMPTY", "Container emptied - ready for operation", distance);
  }
  
  // Stop system if container becomes full during operation
  if (systemReady && containerFull) {
    Serial.println("Stopping operation - Container full");
    pauseSystemForFullContainer();
  }
  
  lastLevelCheck = millis();
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
  
  Serial.print("Servo command sent: ");
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
  
  // Stop process if container becomes full
  if (systemReady && containerFull) {
    pauseSystemForFullContainer();
  }
}

void completeTask() {
  isCompletionDispensing = false;
  Serial.println("Task completed - Resetting system");
  
  systemReady = false;
  taskCompleted = false;
  bottleCount = 0;
  greenBottleDetected = false;
  startButton.pressed = false;
  sessionStartTime = 0;
  lcd.clear();
}

void sendBottleData() {
  if (!wifiConnected) {
    Serial.println("Cannot send bottle data - WiFi not connected");
    return;
  }
  
  // Additional check for valid IP
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("Cannot send bottle data - No valid IP address");
    return;
  }
  
  Serial.println("Sending bottle data to API...");
  
  // Create JSON payload with ISO timestamp
  String jsonPayload = "{";
  jsonPayload += "\"sessionType\": \"Arduino Collection\", ";
  jsonPayload += "\"bottles\": " + String(TARGET_BOTTLES) + ", ";
  jsonPayload += "\"status\": \"Completed\", ";
  jsonPayload += "\"device_id\": \"ecobot_001\", ";
  jsonPayload += "\"timestamp\": \"" + getFormattedISODate() + "\"";
  jsonPayload += "}";
  
  Serial.println("JSON Payload:");
  Serial.println(jsonPayload);
  
  // Send HTTP request
  httpClient.beginRequest();
  httpClient.post(bottleDataEndpoint);
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", jsonPayload.length());
  httpClient.beginBody();
  httpClient.print(jsonPayload);
  httpClient.endRequest();
  
  // Handle response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();
  
  Serial.print("HTTP Status: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  
  if (statusCode == 200 || statusCode == 201) {
    Serial.println("Bottle data sent successfully!");
  } else {
    Serial.println("Failed to send bottle data");
    Serial.print("Status code: ");
    Serial.println(statusCode);
  }
}

void updateDisplay() {
  if (containerFull) {
    displayMessage("   BIN IS FULL     ", "PLEASE EMPTY BIN");
    return;
  }
  
  if (!systemReady) {
    displayMessage("WELCOME TO ECOBOT", "PRESS TO START  ");
  } else if (isCompletionDispensing && servoInAction) {
    displayMessage("Task Complete!  ", "Dispensing...   ");
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
    lcd.print("/3 ");
    lcd.setCursor(11, 1);
    lcd.print(" OK ");
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
    Serial.println("Buzzer activated - Bottle rejected!");
  }
}

void handleBuzzer() {
  if (!buzzer.active) return;
  
  unsigned long currentTime = millis();
  
  if (currentTime - buzzer.startTime >= BUZZER_DURATION) {
    digitalWrite(BUZZER_PIN, LOW);
    noTone(BUZZER_PIN);
    buzzer.active = false;
    Serial.println("Buzzer deactivated");
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

// Utility functions
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
    
    // Wait a bit more and try again
    delay(2000);
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
