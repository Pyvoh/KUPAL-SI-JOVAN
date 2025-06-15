#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

// WiFi credentials
const char* ssid = "Pyvoh.";        // Replace with your WiFi SSID
const char* password = "waitlang"; // Replace with your WiFi password

// Web server details
const char* serverAddress = "ecobot-idnm.onrender.com"; // Replace with your web app domain
const int serverPort = 443;
const char* notificationEndpoint = "/api/bin-status"; // Replace with your API endpoint

WiFiClient wifiClient;
HttpClient httpClient = HttpClient(wifiClient, serverAddress, serverPort);

// WiFi connection status
bool wifiConnected = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; // Check WiFi every 30 seconds

// Notification tracking
bool lastNotificationSent = false;
unsigned long lastNotificationTime = 0;
const unsigned long NOTIFICATION_COOLDOWN = 60000; // 1 minute cooldown between notifications

// SoftwareSerial for communication with slave Arduino (R3 compatible)
SoftwareSerial slaveSerial(12, A1); // RX on pin 12, TX on pin A1

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Buzzer Pin
const int BUZZER_PIN = A0;

// Button Pins
const int START_BUTTON_PIN = 11;
const int RESET_BUTTON_PIN = 3; // Reset button pin

// Start Button variables
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
bool buttonPressed = false; // Variable to track if button was already pressed

// Reset Button variables
bool resetButtonState = HIGH;
bool lastResetButtonState = HIGH;
unsigned long lastResetDebounceTime = 0;

// IR + Ultrasonic Pins
const int IR_PIN = 2;
const int TRIG_PIN = 9;
const int ECHO_PIN = 10;
const int LED_PIN = 13;

// TCS3200 color sensor pins
#define S0 4
#define S1 5
#define S2 6
#define S3 7
#define sensorOut 8

int redFreq, greenFreq, blueFreq;

// Color sensor timing variables
unsigned long lastColorScanTime = 0;
const unsigned long COLOR_SCAN_DELAY = 2000; // 2 seconds delay between color scans

// Bottle Counter
int bottleCount = 0;
int lastIRReading = HIGH;
int currentIRReading = HIGH;
unsigned long lastCountTime = 0;
const unsigned long COUNT_DELAY = 1000;

// Target bottle count and completion status
const int TARGET_BOTTLES = 3;
bool taskCompleted = false;
unsigned long completionTime = 0;
bool systemReady = false;

// Level Check - MOVED OUTSIDE systemReady condition
bool lastContainerStatus = false;
unsigned long lastLevelCheck = 0;
const unsigned long LEVEL_CHECK_INTERVAL = 500;
const float CONTAINER_HEIGHT = 20.0;
const float FULL_THRESHOLD = 10.0;

const int READINGS_TO_AVERAGE = 3;
float distanceReadings[READINGS_TO_AVERAGE];
int readingIndex = 0;

// Color Detection
String colorStatus = "CHECKING";
bool shouldCount = true;
bool isRejecting = false;
bool isDispensing = false;
bool isCompletionDispensing = false;

// Servo Control Commands
#define SERVO_REJECT_CMD 1
#define SERVO_DISPENSE_CMD 2
#define SERVO_RESET_CMD 3
#define SERVO_STATUS_CMD 4
#define SERVO_COUNTER_INCREMENT_CMD 5
#define SERVO_EMPTY_CMD 6

// Servo status tracking
bool servoInAction = false;
unsigned long servoCommandTime = 0;
const unsigned long SERVO_ACTION_TIMEOUT = 8000; // 8 seconds total timeout

// Green bottle detection
bool greenBottleDetected = false;
unsigned long greenDetectionTime = 0;
const unsigned long GREEN_BLOCK_DURATION = 3000;

// Buzzer variables
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
const unsigned long BUZZER_DURATION = 5000;
int buzzerToneCount = 0;
unsigned long lastBuzzerTone = 0;
const unsigned long BUZZER_TONE_INTERVAL = 200;
const int BUZZER_FREQUENCY = 1000;

void setup() {
  Serial.begin(9600);
  slaveSerial.begin(9600); // Initialize SoftwareSerial communication for R3

  // Initialize WiFi
  initializeWiFi();

  // Button setup
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Reset button setup

  // IR & Ultrasonic
  pinMode(IR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  // Buzzer setup
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Color sensor
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("WELCOME TO ECOBOT");
  lcd.setCursor(0, 1);
  lcd.print("PRESS TO START");

  // Initialize ultrasonic readings
  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = getSingleUltrasonicReading();
    delay(100);
  }
  
  // Reset servo to default position
  sendServoCommand(SERVO_RESET_CMD);
  
  // Initialize color scan timer
  lastColorScanTime = millis();
  
  Serial.println("Master Arduino Ready - ECOBOT (3 bottles mode)");
  Serial.println("Ultrasonic sensor active from start");
  Serial.println("Color sensor with 3-second scan delay enabled");
  Serial.println("Reset button available on pin 3");
  Serial.print("WiFi Status: ");
  Serial.println(wifiConnected ? "Connected" : "Disconnected");
}

void loop() {
  handleButtonInput();
  handleResetButton(); // Function call for reset button
  
  // Check WiFi connection periodically
  checkWiFiConnection();
  
  // MOVED OUTSIDE systemReady condition - ultrasonic works from beginning
  checkContainerLevel();
  
  // Only proceed with bottle processing if container is not full
  if (systemReady && !lastContainerStatus) {
    detectColor();
    handleBottleCounting();
    checkServoStatus();
  }
  
  handleBuzzer();
  updateLCD();
  delay(50);
}

void initializeWiFi() {
  Serial.print("Attempting to connect to WiFi network: ");
  Serial.println(ssid);
  
  // Disconnect any previous connection
  WiFi.disconnect();
  delay(1000);
  
  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Begin WiFi connection with timeout
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 30; // Increase attempts
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(1000);
    Serial.print(".");
    attempts++;
    
    // Print current status for debugging
    if (attempts % 5 == 0) {
      Serial.print(" [Status: ");
      Serial.print(WiFi.status());
      Serial.print("] ");
    }
    
    // Try to reconnect every 10 attempts
    if (attempts % 10 == 0 && attempts < maxAttempts) {
      Serial.println();
      Serial.println("Retrying WiFi connection...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
  } else {
    wifiConnected = false;
    Serial.println();
    Serial.println("Failed to connect to WiFi after maximum attempts");
    Serial.print("Final WiFi status: ");
    Serial.println(WiFi.status());
    Serial.println("WiFi Status Codes:");
    Serial.println("0 = WL_IDLE_STATUS");
    Serial.println("1 = WL_NO_SSID_AVAIL");
    Serial.println("2 = WL_SCAN_COMPLETED");
    Serial.println("3 = WL_CONNECTED");
    Serial.println("4 = WL_CONNECT_FAILED");
    Serial.println("5 = WL_CONNECTION_LOST");
    Serial.println("6 = WL_DISCONNECTED");
    Serial.println("System will continue without WiFi functionality");
  }
}

void checkWiFiConnection() {
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  
  lastWiFiCheck = millis();
  
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    Serial.println("WiFi connection lost! Attempting to reconnect...");
    
    // Attempt immediate reconnection
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    // Wait up to 10 seconds for reconnection
    int reconnectAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && reconnectAttempts < 10) {
      delay(1000);
      reconnectAttempts++;
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println();
      Serial.println("WiFi reconnected successfully!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println();
      Serial.println("Failed to reconnect to WiFi");
    }
    
  } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    Serial.println("WiFi reconnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void sendBinFullNotification() {
  if (!wifiConnected) {
    Serial.println("Cannot send notification - WiFi not connected");
    return;
  }
  
  // Check cooldown period to avoid spam
  if (lastNotificationSent && (millis() - lastNotificationTime) < NOTIFICATION_COOLDOWN) {
    return;
  }
  
  Serial.println("Sending bin full notification to web app...");
  
  // Create JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"status\":\"full\",";
  jsonPayload += "\"message\":\"Bin is full - please empty\",";
  jsonPayload += "\"timestamp\":\"" + String(millis()) + "\",";
  jsonPayload += "\"device_id\":\"ecobot_001\"";
  jsonPayload += "}";
  
  // Make HTTP POST request
  httpClient.beginRequest();
  httpClient.post(notificationEndpoint);
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", jsonPayload.length());
  httpClient.beginBody();
  httpClient.print(jsonPayload);
  httpClient.endRequest();
  
  // Read response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();
  
  Serial.print("HTTP Status Code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  
  if (statusCode == 200 || statusCode == 201) {
    Serial.println("Notification sent successfully!");
    lastNotificationSent = true;
    lastNotificationTime = millis();
  } else {
    Serial.println("Failed to send notification");
  }
}

void sendBinEmptyNotification() {
  if (!wifiConnected) {
    Serial.println("Cannot send notification - WiFi not connected");
    return;
  }
  
  Serial.println("Sending bin empty notification to web app...");
  
  // Create JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"status\":\"empty\",";
  jsonPayload += "\"message\":\"Bin is now empty - ready for use\",";
  jsonPayload += "\"timestamp\":\"" + String(millis()) + "\",";
  jsonPayload += "\"device_id\":\"ecobot_001\"";
  jsonPayload += "}";
  
  // Make HTTP POST request
  httpClient.beginRequest();
  httpClient.post(notificationEndpoint);
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", jsonPayload.length());
  httpClient.beginBody();
  httpClient.print(jsonPayload);
  httpClient.endRequest();
  
  // Read response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();
  
  Serial.print("HTTP Status Code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  
  if (statusCode == 200 || statusCode == 201) {
    Serial.println("Empty notification sent successfully!");
    lastNotificationSent = false; // Reset so we can send full notification again
  } else {
    Serial.println("Failed to send empty notification");
  }
}

void handleButtonInput() {
  bool reading = digitalRead(START_BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      // Only respond to button press if it hasn't been pressed in this procedure and container is not full
      if (buttonState == LOW && !taskCompleted && !buttonPressed && !lastContainerStatus) {
        buttonPressed = true; // Mark button as pressed for this procedure
        systemReady = true;
        bottleCount = 0;
        taskCompleted = false;
        greenBottleDetected = false;
        servoInAction = false;
        lastColorScanTime = millis(); // Reset color scan timer when starting
        Serial.println("ECOBOT STARTED - 3 BOTTLES MODE");
        lcd.clear();
      }
      else if (buttonState == LOW && lastContainerStatus) {
        Serial.println("Cannot start - Container is full! Please empty the container.");
      }
    }
  }
  lastButtonState = reading;
}

void handleResetButton() {
  bool reading = digitalRead(RESET_BUTTON_PIN);
  if (reading != lastResetButtonState) {
    lastResetDebounceTime = millis();
  }
  
  if ((millis() - lastResetDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != resetButtonState) {
      resetButtonState = reading;
      if (resetButtonState == LOW && systemReady) { // Only reset if system is currently running
        // Reset all system variables
        systemReady = false;
        taskCompleted = false;
        bottleCount = 0;
        greenBottleDetected = false;
        servoInAction = false;
        isRejecting = false;
        isDispensing = false;
        isCompletionDispensing = false;
        buttonPressed = false;
        shouldCount = true;
        colorStatus = "CHECKING";
        
        // Turn off buzzer if active
        if (buzzerActive) {
          buzzerActive = false;
          digitalWrite(BUZZER_PIN, LOW);
          noTone(BUZZER_PIN);
        }
        
        // Send empty command to rotate MG996R to 120 degrees and return to default after 2 seconds
        sendServoCommand(SERVO_EMPTY_CMD);
        
        // Clear LCD
        lcd.clear();
        
        Serial.println("SYSTEM RESET - Bottles cleared, returning to welcome screen");
        Serial.println("MG996R rotating to 120 degrees to empty container");
        Serial.print("Bottle count reset from previous count to 0");
      }
    }
  }
  lastResetButtonState = reading;
}

void detectColor() {
  // Check if enough time has passed since last color scan
  if (millis() - lastColorScanTime < COLOR_SCAN_DELAY) {
    return; // Skip color detection if delay hasn't elapsed
  }
  
  // Update last scan time
  lastColorScanTime = millis();
  
  Serial.println("Performing color scan...");
  
  // Read RED frequency
  digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  redFreq = pulseIn(sensorOut, LOW, 50000);

  // Read GREEN frequency
  digitalWrite(S2, HIGH); digitalWrite(S3, HIGH);
  greenFreq = pulseIn(sensorOut, LOW, 50000);

  // Read BLUE frequency
  digitalWrite(S2, LOW); digitalWrite(S3, HIGH);
  blueFreq = pulseIn(sensorOut, LOW, 50000);

  // Debug output for color readings
  Serial.print("Color readings - Red: ");
  Serial.print(redFreq);
  Serial.print(", Green: ");
  Serial.print(greenFreq);
  Serial.print(", Blue: ");
  Serial.println(blueFreq);

  // Color detection logic
  if (greenFreq < redFreq && greenFreq < blueFreq && greenFreq > 0) {
    colorStatus = "GREEN-REJECT";
    shouldCount = false;
    greenBottleDetected = true;
    greenDetectionTime = millis();
    
    Serial.println("GREEN BOTTLE DETECTED - SENDING REJECT COMMAND (0°)");
    sendServoCommand(SERVO_REJECT_CMD);
    isRejecting = true;
    activateBuzzer();
  }
  else if (blueFreq < redFreq && blueFreq < greenFreq && blueFreq > 0) {
    colorStatus = "BLUE-ACCEPT";
    if (!greenBottleDetected || (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
      shouldCount = true;
      greenBottleDetected = false;
    } else {
      shouldCount = false;
    }
    isRejecting = false;
  }
  else {
    colorStatus = "OTHER";
    if (!greenBottleDetected || (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
      shouldCount = true;
      greenBottleDetected = false;
    } else {
      shouldCount = false;
    }
    isRejecting = false;
  }
  
  if (greenBottleDetected && (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
    greenBottleDetected = false;
  }
  
  Serial.print("Color status: ");
  Serial.println(colorStatus);
}

void handleBottleCounting() {
  currentIRReading = digitalRead(IR_PIN);
  
  if (currentIRReading == LOW && lastIRReading == HIGH &&
      shouldCount && 
      !greenBottleDetected &&
      (millis() - lastCountTime) > COUNT_DELAY) {
    
    bottleCount++;
    lastCountTime = millis();
    
    // Check if this is the final bottle (3rd bottle)
    if (bottleCount >= TARGET_BOTTLES) {
      taskCompleted = true;
      completionTime = millis();
      isCompletionDispensing = true;
      
      Serial.print("TARGET REACHED - SENDING DISPENSE COMMAND (MG996R to 90°, SG90 to 90°) (");
      Serial.print(TARGET_BOTTLES);
      Serial.println(" bottles)");
      
      // For the 3rd bottle, send DISPENSE command directly (no counter increment)
      // This will move both MG996R to 90° and SG90 to 90°
      sendServoCommand(SERVO_DISPENSE_CMD);
    } else {
      // For bottles 1 and 2, send counter increment command (MG996R to 90°)
      Serial.print("BOTTLE COUNTED - SENDING COUNTER INCREMENT COMMAND (90°) (Count: ");
      Serial.print(bottleCount);
      Serial.println(")");
      sendServoCommand(SERVO_COUNTER_INCREMENT_CMD);
    }
    
    Serial.print("Bottle ACCEPTED! Color: ");
    Serial.print(colorStatus);
    Serial.print(", Count: ");
    Serial.print(bottleCount);
    Serial.print("/");
    Serial.println(TARGET_BOTTLES);
  }
  else if (currentIRReading == LOW && lastIRReading == HIGH && 
           (colorStatus == "GREEN-REJECT" || greenBottleDetected)) {
    Serial.println("GREEN BOTTLE REJECTED - NOT COUNTED!");
  }
  
  lastIRReading = currentIRReading;
}

void checkContainerLevel() {
  if (millis() - lastLevelCheck < LEVEL_CHECK_INTERVAL) return;

  float distance = getStableUltrasonicDistance();
  bool isFull;

  if (lastContainerStatus) {
    isFull = (distance <= FULL_THRESHOLD + 1.0 && distance > 0);
  } else {
    isFull = (distance <= FULL_THRESHOLD && distance > 0);
  }

  digitalWrite(LED_PIN, isFull ? HIGH : LOW);
  
  // Check for status change and send notifications
  if (isFull && !lastContainerStatus) {
    // Bin just became full
    Serial.println("BIN IS NOW FULL - SENDING NOTIFICATION");
    sendBinFullNotification();
  } else if (!isFull && lastContainerStatus) {
    // Bin just became empty
    Serial.println("BIN IS NOW EMPTY - SENDING NOTIFICATION");
    sendBinEmptyNotification();
  }
  
  // Debug output for ultrasonic readings
  if (millis() % 2000 == 0) { // Print every 2 seconds
    Serial.print("Container distance: ");
    Serial.print(distance);
    Serial.print(" cm, Status: ");
    Serial.println(isFull ? "FULL" : "");
  }
  
  lastContainerStatus = isFull;
  lastLevelCheck = millis();
}

float getStableUltrasonicDistance() {
  float newReading = getSingleUltrasonicReading();
  if (newReading > 0) {
    distanceReadings[readingIndex] = newReading;
    readingIndex = (readingIndex + 1) % READINGS_TO_AVERAGE;

    float sum = 0;
    for (int i = 0; i < READINGS_TO_AVERAGE; i++) sum += distanceReadings[i];
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
  slaveSerial.print(command); // Using slaveSerial instead of Serial1
  slaveSerial.println(); // Send newline as delimiter
  
  servoInAction = true;
  servoCommandTime = millis();
  
  Serial.print("Sent servo command: ");
  Serial.println(command);
}

void checkServoStatus() {
  // Check if servo action has timed out
  if (servoInAction && (millis() - servoCommandTime) > SERVO_ACTION_TIMEOUT) {
    servoInAction = false;
    isRejecting = false;
    isDispensing = false;
    
    if (taskCompleted && isCompletionDispensing) {
      isCompletionDispensing = false;
      Serial.println("Task completed - Resetting system");
      systemReady = false;
      taskCompleted = false;
      bottleCount = 0;
      greenBottleDetected = false;
      buttonPressed = false; // Reset button pressed flag when procedure completes
      lcd.clear();
    }
  }
  
  // If container becomes full during operation, stop the process
  if (systemReady && lastContainerStatus) {
    Serial.println("Container became full - Stopping process");
    systemReady = false;
    taskCompleted = false;
    servoInAction = false;
    isRejecting = false;
    isDispensing = false;
    isCompletionDispensing = false;
    buttonPressed = false;
    lcd.clear();
  }
  
  // Optionally request status from slave
  if (servoInAction && (millis() - servoCommandTime) % 1000 == 0) {
    requestServoStatus();
  }
}

void requestServoStatus() {
  slaveSerial.println("STATUS"); // Using slaveSerial instead of Serial1
  delay(10); // Small delay for response
  
  if (slaveSerial.available()) { // Using slaveSerial instead of Serial1
    String response = slaveSerial.readStringUntil('\n');
    int status = response.toInt();
    
    if (status == 0) { // Servo action completed
      servoInAction = false;
      isRejecting = false;
      isDispensing = false;
      
      if (taskCompleted && isCompletionDispensing) {
        isCompletionDispensing = false;
        Serial.println("Servo action completed - Resetting system");
        systemReady = false;
        taskCompleted = false;
        bottleCount = 0;
        greenBottleDetected = false;
        buttonPressed = false; // Reset button pressed flag when procedure completes
        lcd.clear();
      }
    }
  }
}

void updateLCD() {
  // If container is full, always display "BIN IS FULL" regardless of system state
  if (lastContainerStatus) {
    lcd.setCursor(0, 0);
    lcd.print("   BIN IS FULL     ");
    lcd.setCursor(0, 1);
    lcd.print("PLEASE EMPTY BIN");
    return;
  }
  
  if (!systemReady) {
    lcd.setCursor(0, 0);
    lcd.print("WELCOME TO ECOBOT");
    lcd.setCursor(0, 1);
    lcd.print("PRESS TO START  ");
  } else if (isCompletionDispensing && servoInAction) {
    lcd.setCursor(0, 0);
    lcd.print("Task Complete!  ");
    lcd.setCursor(0, 1);
    lcd.print("Dispensing...   ");
  } else if (taskCompleted && !isCompletionDispensing) {
    lcd.setCursor(0, 0);
    lcd.print("Task Complete!  ");
    lcd.setCursor(0, 1);
    lcd.print("Resetting...    ");
  } else if (isRejecting && servoInAction) {
    lcd.setCursor(0, 0);
    lcd.print("GREEN BOTTLE    ");
    lcd.setCursor(0, 1);
    lcd.print("REJECTED!       ");
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

void activateBuzzer() {
  if (!buzzerActive) {
    buzzerActive = true;
    buzzerStartTime = millis();
    buzzerToneCount = 0;
    lastBuzzerTone = millis();
    Serial.println("BUZZER ACTIVATED - Bottle rejected!");
  }
}

void handleBuzzer() {
  if (buzzerActive) {
    unsigned long currentTime = millis();
    
    if (currentTime - buzzerStartTime >= BUZZER_DURATION) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerActive = false;
      Serial.println("BUZZER DEACTIVATED");
      return;
    }
    
    if (buzzerToneCount < 5 && (currentTime - lastBuzzerTone) >= BUZZER_TONE_INTERVAL) {
      if (buzzerToneCount % 2 == 0) {
        tone(BUZZER_PIN, BUZZER_FREQUENCY);
      } else {
        noTone(BUZZER_PIN);
      }
      buzzerToneCount++;
      lastBuzzerTone = currentTime;
    }
    
    if (buzzerToneCount >= 5) {
      noTone(BUZZER_PIN);
    }
  }
}
