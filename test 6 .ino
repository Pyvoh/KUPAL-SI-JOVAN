#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servo Motor
Servo rejectServo;
const int SERVO_PIN = 3;
const int SERVO_REJECT_ANGLE = 110;
const int SERVO_DEFAULT_ANGLE = 45;
const int SERVO_COUNT_ANGLE = 0;  // New angle for when counter reaches 3

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
const unsigned long SERVO_DELAY_DURATION = 5000; // 5 seconds delay before servo activates
const unsigned long SERVO_ACTIVE_DURATION = 4000; // Servo stays at 45° for 4 seconds

// Counter-based servo control
bool counterServoActive = false;
bool counterServoDelaying = false;
unsigned long counterServoActionTime = 0;
unsigned long counterServoDelayTime = 0;
const unsigned long COUNTER_SERVO_DELAY = 4000; // 4 seconds delay before servo moves to 0 degrees
const unsigned long COUNTER_SERVO_DURATION = 5000; // 5 seconds at 0 degrees
bool counterReached = false;

void setup() {
  Serial.begin(9600);

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

  // Servo Motor
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE); // Start at default position

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Starting");

  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = getSingleUltrasonicReading();
    delay(100);
  }
  lcd.clear();
}

void loop() {
  detectColor();
  handleBottleCounting();
  checkContainerLevel();
  handleServoMovement();
  handleCounterServo();
  updateLCD();
  delay(50);
}

void detectColor() {
  // Read RED frequency
  digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  redFreq = pulseIn(sensorOut, LOW);

  // Read GREEN frequency
  digitalWrite(S2, HIGH); digitalWrite(S3, HIGH);
  greenFreq = pulseIn(sensorOut, LOW);

  // Read BLUE frequency
  digitalWrite(S2, LOW); digitalWrite(S3, HIGH);
  blueFreq = pulseIn(sensorOut, LOW);

  // Color detection logic - lower frequency means stronger color
  // Detect GREEN (reset counter and activate servo)
  if (greenFreq < redFreq && greenFreq < blueFreq) {
    colorStatus = "GREEN-REJECT";
    shouldCount = false;
    // Reset bottle count to zero when green is detected
    if (bottleCount > 0) {
      bottleCount = 0;
      counterReached = false; // Reset counter flag
      counterServoDelaying = false; // Reset delay flag
      Serial.println("GREEN DETECTED - Counter RESET to 0!");
    }
    // Activate servo to reject the bottle
    activateRejectServo();
  }
  // Detect BLUE (accept bottle)
  else if (blueFreq < redFreq && blueFreq < greenFreq) {
    colorStatus = "BLUE-ACCEPT";
    shouldCount = true; // Count blue bottles
  }
  // Other colors or no clear detection
  else {
    colorStatus = "OTHER";
    shouldCount = false; // Don't count other colors by default
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
    
    // Check if counter reached 3
    if (bottleCount == 3 && !counterReached) {
      activateCounterServo();
      counterReached = true;
    }
    
    // Optional: Print debug info
    Serial.print("Bottle counted! Color: ");
    Serial.print(colorStatus);
    Serial.print(", Total count: ");
    Serial.println(bottleCount);
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
  lcd.setCursor(0, 0);
  lcd.print("Color: ");
  if (colorStatus.length() <= 8) {
    lcd.print(colorStatus);
    // Clear any remaining characters
    for (int i = colorStatus.length(); i < 8; i++) {
      lcd.print(" ");
    }
  } else {
    // Truncate if too long
    lcd.print(colorStatus.substring(0, 8));
  }

  lcd.setCursor(0, 1);
  lcd.print("Count:");
  lcd.print(bottleCount);
  lcd.print(" ");

  lcd.setCursor(11, 1);
  lcd.print(lastContainerStatus ? "FULL" : " OK ");
}

void activateRejectServo() {
  if (!servoActive && !servoDelaying && !counterServoActive) {
    servoDelayTime = millis();
    servoDelaying = true;
    Serial.println("GREEN DETECTED - Starting 5 second delay before servo activation...");
  }
}

void activateCounterServo() {
  if (!counterServoActive && !counterServoDelaying && !servoActive && !servoDelaying) {
    counterServoDelayTime = millis();
    counterServoDelaying = true;
    Serial.println("COUNTER REACHED 3 - Starting 4 second delay before servo moves to 0 degrees...");
  }
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && !counterServoActive && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // 5 seconds delay completed, now activate servo
    rejectServo.write(SERVO_REJECT_ANGLE); // Rotate to 95 degrees
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    Serial.println("SERVO ACTIVATED - Rejecting GREEN bottle!");
  }
  
  // Return servo to default position after specified duration
  if (servoActive && (millis() - servoActionTime) >= SERVO_ACTIVE_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE); // Return to default degrees
    servoActive = false;
    Serial.println("SERVO RETURNED to default position");
  }
}

void handleCounterServo() {
  // Handle delay before counter servo activation
  if (counterServoDelaying && (millis() - counterServoDelayTime) >= COUNTER_SERVO_DELAY) {
    // 4 seconds delay completed, now activate servo
    rejectServo.write(SERVO_COUNT_ANGLE); // Rotate to 0 degrees
    counterServoActionTime = millis();
    counterServoActive = true;
    counterServoDelaying = false;
    Serial.println("COUNTER SERVO ACTIVATED - Moving to 0 degrees!");
  }
  
  // Return counter servo to default position after 5 seconds
  if (counterServoActive && (millis() - counterServoActionTime) >= COUNTER_SERVO_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE); // Return to default position
    counterServoActive = false;
    Serial.println("COUNTER SERVO RETURNED to default position");
  }
}
