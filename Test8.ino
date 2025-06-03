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
  // Detect GREEN (reject bottle - don't reset counter during counting)
  if (greenFreq < redFreq && greenFreq < blueFreq && greenFreq > 0) {
    colorStatus = "GREEN-REJECT";
    shouldCount = false;
    
    // Only reset counter if no bottles have been inserted yet
    // During counting process, just reject without resetting
    if (bottleCount == 0) {
      Serial.println("GREEN DETECTED - Not counting green bottles");
    } else {
      Serial.println("GREEN DETECTED during counting - REJECTING bottle, maintaining count");
    }
    
    // Activate servo to reject the bottle
    activateRejectServo();
  }
  // Detect BLUE (accept bottle)
  else if (blueFreq < redFreq && blueFreq < greenFreq && blueFreq > 0) {
    colorStatus = "BLUE-ACCEPT";
    shouldCount = true; // Count blue bottles
    isRejecting = false; // Clear rejection status
  }
  // Other colors or no clear detection
  else {
    colorStatus = "OTHER";
    shouldCount = false; // Don't count other colors by default
    isRejecting = false; // Clear rejection status
  }
}

void handleBottleCounting() {
  currentIRReading = digitalRead(IR_PIN);
  
  // Only count if:
  // 1. IR sensor detects transition (object passes)
  // 2. Color sensor says we should count (blue detected)
  // 3. Enough time has passed since last count
  if (currentIRReading == LOW && lastIRReading == HIGH &&
      shouldCount && 
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
    Serial.print("Bottle counted! Color: ");
    Serial.print(colorStatus);
    Serial.print(", Total count: ");
    Serial.print(bottleCount);
    Serial.print("/");
    Serial.println(TARGET_BOTTLES);
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
      lcd.clear();
    } else {
      Serial.println("MG996R SERVO RETURNED to default position");
    }
  }
}
