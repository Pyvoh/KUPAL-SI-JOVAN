#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MG996R Servo Motor
Servo rejectServo;
const int SERVO_PIN = 3;
const int SERVO_REJECT_ANGLE = 60;  // MG996R works better at 90 degrees
const int SERVO_DEFAULT_ANGLE = 0;

// Buzzer Pin
const int BUZZER_PIN = A0;  // Using analog pin A0 for buzzer

// Button Pins
const int BUTTON1_PIN = 11;  // Button for 3 bottles
const int BUTTON2_PIN = 12;  // Button for 5 bottles

// Button variables
bool button1State = HIGH;
bool button2State = HIGH;
bool lastButton1State = HIGH;
bool lastButton2State = HIGH;
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long DEBOUNCE_DELAY = 50;

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

// Bottle Counter
int bottleCount = 0;
int lastIRReading = HIGH;
int currentIRReading = HIGH;
unsigned long lastCountTime = 0;
const unsigned long COUNT_DELAY = 1000;
int lastDisplayedCount = -1;

// Target bottle count and completion status
int TARGET_BOTTLES = 3;  // Default to 3 bottles
bool taskCompleted = false;
unsigned long completionTime = 0;
bool systemReady = false;  // Flag to check if mode is selected

// Level Check
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
bool shouldCount = true; // Flag to control counting based on color
unsigned long servoActionTime = 0;
unsigned long servoDelayTime = 0;
bool servoActive = false;
bool servoDelaying = false;
bool isRejecting = false; // Flag to show rejection status on LCD
bool isDispensing = false; // Flag to show dispensing status on LCD
bool isCompletionDispensing = false; // New flag specifically for completion dispensing
const unsigned long SERVO_DELAY_DURATION = 2000; // 2 seconds delay before servo activates
const unsigned long SERVO_ACTIVE_DURATION = 4000; // MG996R stays at angle for 4 seconds (longer for stronger servo)

// Additional variables to prevent green bottle counting
bool greenBottleDetected = false;
unsigned long greenDetectionTime = 0;
const unsigned long GREEN_BLOCK_DURATION = 3000; // Block counting for 3 seconds after green detection

// Buzzer variables
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
const unsigned long BUZZER_DURATION = 5000; // Buzzer sounds for 1 second
int buzzerToneCount = 0;
unsigned long lastBuzzerTone = 0;
const unsigned long BUZZER_TONE_INTERVAL = 200; // Beep every 200ms for alert pattern
const int BUZZER_FREQUENCY = 1000; // 1kHz tone

void setup() {
  Serial.begin(9600);

  // Button setup
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // IR & Ultrasonic
  pinMode(IR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  // Buzzer setup
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially

  // Color sensor
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);

  // MG996R Servo Motor
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE); // Start at default position
  delay(500); // Give servo time to reach position

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("ECOBOT Ready");
  lcd.setCursor(0, 1);
  lcd.print("Select Mode...");

  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = getSingleUltrasonicReading();
    delay(100);
  }
  
  Serial.println("System Ready - Please select mode:");
  Serial.println("Button 1: 3 bottles mode");
  Serial.println("Button 2: 5 bottles mode");
}

void loop() {
  handleButtonInput();
  
  if (systemReady) {
    detectColor();
    handleBottleCounting();
    checkContainerLevel();
    handleServoMovement();
  }
  
  handleBuzzer(); // Handle buzzer operation
  updateLCD();
  delay(50);
}

void handleButtonInput() {
  // Handle Button 1 (3 bottles mode)
  bool reading1 = digitalRead(BUTTON1_PIN);
  if (reading1 != lastButton1State) {
    lastDebounceTime1 = millis();
  }
  
  if ((millis() - lastDebounceTime1) > DEBOUNCE_DELAY) {
    if (reading1 != button1State) {
      button1State = reading1;
      if (button1State == LOW && !taskCompleted) { // Button pressed
        TARGET_BOTTLES = 3;
        systemReady = true;
        bottleCount = 0; // Reset count
        taskCompleted = false;
        greenBottleDetected = false; // Reset green detection
        Serial.println("Mode Selected: 3 BOTTLES");
        lcd.clear();
      }
    }
  }
  lastButton1State = reading1;

  // Handle Button 2 (5 bottles mode)
  bool reading2 = digitalRead(BUTTON2_PIN);
  if (reading2 != lastButton2State) {
    lastDebounceTime2 = millis();
  }
  
  if ((millis() - lastDebounceTime2) > DEBOUNCE_DELAY) {
    if (reading2 != button2State) {
      button2State = reading2;
      if (button2State == LOW && !taskCompleted) { // Button pressed
        TARGET_BOTTLES = 5;
        systemReady = true;
        bottleCount = 0; // Reset count
        taskCompleted = false;
        greenBottleDetected = false; // Reset green detection
        Serial.println("Mode Selected: 5 BOTTLES");
        lcd.clear();
      }
    }
  }
  lastButton2State = reading2;
}

void detectColor() {
  // Read RED frequency
  digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  redFreq = pulseIn(sensorOut, LOW, 50000); // Add timeout for stability

  // Read GREEN frequency
  digitalWrite(S2, HIGH); digitalWrite(S3, HIGH);
  greenFreq = pulseIn(sensorOut, LOW, 50000);

  // Read BLUE frequency
  digitalWrite(S2, LOW); digitalWrite(S3, HIGH);
  blueFreq = pulseIn(sensorOut, LOW, 50000);

  // Color detection logic - lower frequency means stronger color
  // Detect GREEN (reject bottle - NEVER count green bottles)
  if (greenFreq < redFreq && greenFreq < blueFreq && greenFreq > 0) {
    colorStatus = "GREEN-REJECT";
    shouldCount = false; // NEVER count green bottles
    greenBottleDetected = true;
    greenDetectionTime = millis();
    
    Serial.println("GREEN BOTTLE DETECTED - REJECTING (Will not be counted)");
    
    // Activate servo to reject the bottle
    activateRejectServo();
  }
  // Detect BLUE (accept bottle)
  else if (blueFreq < redFreq && blueFreq < greenFreq && blueFreq > 0) {
    colorStatus = "BLUE-ACCEPT";
    // Only allow counting if no green bottle was recently detected
    if (!greenBottleDetected || (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
      shouldCount = true; // Count blue bottles
      greenBottleDetected = false; // Clear green detection after sufficient time
    } else {
      shouldCount = false; // Still blocking due to recent green detection
    }
    isRejecting = false; // Clear rejection status
  }
  // Other colors or no clear detection
  else {
    colorStatus = "OTHER";
    // Only allow counting of other colors if no green bottle was recently detected
    if (!greenBottleDetected || (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
      shouldCount = true; // Allow counting of other acceptable colors
      greenBottleDetected = false; // Clear green detection after sufficient time
    } else {
      shouldCount = false; // Still blocking due to recent green detection
    }
    isRejecting = false; // Clear rejection status
  }
  
  // Clear green detection flag after blocking period
  if (greenBottleDetected && (millis() - greenDetectionTime) > GREEN_BLOCK_DURATION) {
    greenBottleDetected = false;
  }
}

void handleBottleCounting() {
  currentIRReading = digitalRead(IR_PIN);
  
  // Only count if:
  // 1. IR sensor detects transition (object passes)
  // 2. Color sensor says we should count (NOT green and not recently green)
  // 3. Enough time has passed since last count
  // 4. No green bottle was recently detected
  if (currentIRReading == LOW && lastIRReading == HIGH &&
      shouldCount && 
      !greenBottleDetected &&
      (millis() - lastCountTime) > COUNT_DELAY) {
    
    bottleCount++;
    lastCountTime = millis();
    
    // Check if target is reached for completion
    if (bottleCount >= TARGET_BOTTLES) {
      taskCompleted = true;
      completionTime = millis();
      isCompletionDispensing = true; // Set completion dispensing flag
      activateCompletionDispenseServo(); // Activate servo for completion
      Serial.print("TARGET REACHED (");
      Serial.print(TARGET_BOTTLES);
      Serial.println(" bottles) - Thank you for using ECOBOT!");
    }
    
    // Print debug info
    Serial.print("Bottle ACCEPTED and counted! Color: ");
    Serial.print(colorStatus);
    Serial.print(", Total count: ");
    Serial.print(bottleCount);
    Serial.print("/");
    Serial.println(TARGET_BOTTLES);
  }
  // If green bottle is detected during IR trigger, explicitly reject
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

void updateLCD() {
  if (!systemReady) {
    // Display mode selection
    lcd.setCursor(0, 0);
    lcd.print("BTN1=3 BTN2=5   ");
    lcd.setCursor(0, 1);
    lcd.print("Select Mode...  ");
  } else if (isCompletionDispensing && (servoDelaying || servoActive)) {
    // Display completion dispensing message when task is complete and servo is moving
    lcd.setCursor(0, 0);
    lcd.print("Task Complete!  ");
    lcd.setCursor(0, 1);
    lcd.print("Dispensing...   ");
  } else if (taskCompleted && !isCompletionDispensing) {
    // Display thank you message after servo action is complete
    lcd.setCursor(0, 0);
    lcd.print("Task Complete!  ");
    lcd.setCursor(0, 1);
    lcd.print("Resetting...    ");
  } else if (isRejecting || (colorStatus == "GREEN-REJECT" && (servoDelaying || servoActive))) {
    // Display rejection message when green bottle is detected
    lcd.setCursor(0, 0);
    lcd.print("GREEN BOTTLE    ");
    lcd.setCursor(0, 1);
    lcd.print("REJECTED!       ");
  } else {
    // Display instruction and current count
    lcd.setCursor(0, 0);
    lcd.print("Insert ");
    lcd.print(TARGET_BOTTLES);
    lcd.print(" bottles");
    
    lcd.setCursor(0, 1);
    lcd.print("Count: ");
    lcd.print(bottleCount);
    lcd.print("/");
    lcd.print(TARGET_BOTTLES);
    lcd.print(" ");
    
    // Show container status
    lcd.setCursor(11, 1);
    lcd.print(lastContainerStatus ? "FULL" : " OK ");
  }
}

void activateRejectServo() {
  if (!servoActive && !servoDelaying) {
    servoDelayTime = millis();
    servoDelaying = true;
    isRejecting = true; // Set rejection flag for LCD display
    activateBuzzer(); // Activate buzzer when rejecting
    Serial.println("GREEN DETECTED - Starting 2 second delay before MG996R servo activation...");
  }
}

void activateCompletionDispenseServo() {
  if (!servoActive && !servoDelaying) {
    servoDelayTime = millis();
    servoDelaying = true;
    isDispensing = true; // Set dispensing flag for LCD display
    Serial.print("COMPLETION REACHED (");
    Serial.print(bottleCount);
    Serial.println(" bottles) - Starting 2 second delay before MG996R servo activation...");
  }
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // 2 seconds delay completed, now activate servo
    rejectServo.write(SERVO_REJECT_ANGLE); // Rotate to 60 degrees
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    
    if (isCompletionDispensing) {
      Serial.print("MG996R SERVO ACTIVATED - Completion dispensing at count ");
      Serial.print(bottleCount);
      Serial.println("!");
    } else if (isRejecting) {
      Serial.println("MG996R SERVO ACTIVATED - Rejecting GREEN bottle!");
    }
  }
  
  // Return servo to default position after specified duration
  if (servoActive && (millis() - servoActionTime) >= SERVO_ACTIVE_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE); // Return to 0 degrees
    servoActive = false;
    isRejecting = false; // Clear rejection flag
    isDispensing = false; // Clear dispensing flag
    
    if (taskCompleted && isCompletionDispensing) {
      isCompletionDispensing = false; // Clear completion dispensing flag
      Serial.println("MG996R SERVO RETURNED - Task completed!");
      Serial.println("Resetting system - Please select new mode");
      // Reset system for next use
      systemReady = false;
      taskCompleted = false;
      bottleCount = 0;
      TARGET_BOTTLES = 3; // Reset to default
      greenBottleDetected = false; // Reset green detection
      lcd.clear();
    } else {
      Serial.println("MG996R SERVO RETURNED to default position");
    }
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
    
    // Check if buzzer duration has expired
    if (currentTime - buzzerStartTime >= BUZZER_DURATION) {
      digitalWrite(BUZZER_PIN, LOW); // Turn off buzzer
      buzzerActive = false;
      Serial.println("BUZZER DEACTIVATED");
      return;
    }
    
    // Create beeping pattern (5 beeps total)
    if (buzzerToneCount < 5 && (currentTime - lastBuzzerTone) >= BUZZER_TONE_INTERVAL) {
      if (buzzerToneCount % 2 == 0) {
        // Turn on buzzer (odd beep number: 1st, 3rd, 5th)
        tone(BUZZER_PIN, BUZZER_FREQUENCY);
      } else {
        // Turn off buzzer (even beep number: 2nd, 4th)
        noTone(BUZZER_PIN);
      }
      buzzerToneCount++;
      lastBuzzerTone = currentTime;
    }
    
    // Turn off buzzer after 5th beep
    if (buzzerToneCount >= 5) {
      noTone(BUZZER_PIN);
    }
  }
}
