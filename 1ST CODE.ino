#include <LiquidCrystal_I2C.h>

// Initialize LCD with I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pin definitions
const int IR_PIN = 2;           // IR sensor output pin
const int TRIG_PIN = 9;         // Ultrasonic trigger pin
const int ECHO_PIN = 10;        // Ultrasonic echo pin
const int LED_PIN = 13; 

// Variables
int bottleCount = 0;            // Counter for bottles
int lastIRReading = HIGH;       // Last stable IR reading
int currentIRReading = HIGH;    // Current IR reading
unsigned long lastCountTime = 0; // Time of last count
const unsigned long COUNT_DELAY = 1000; // Minimum time between counts (1 second)

// Variables for display updates
int lastDisplayedCount = -1;    // Last count shown on LCD
bool lastContainerStatus = false; // Last container full status
unsigned long lastLevelCheck = 0;  // Time of last level check
const unsigned long LEVEL_CHECK_INTERVAL = 500; // Check level every 500ms (more frequent)

// Container parameters (adjust these based on your container)
const float CONTAINER_HEIGHT = 20.0;  // Total height in cm
const float FULL_THRESHOLD = 10.0;    // Distance from sensor when full (10cm)

// Add variables for stable readings
const int READINGS_TO_AVERAGE = 3;    // Number of readings to average
float distanceReadings[READINGS_TO_AVERAGE];
int readingIndex = 0;
bool readingsInitialized = false;

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  
  // Initialize pins
  pinMode(IR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Turn off LED initially
  digitalWrite(LED_PIN, LOW);
  
  // Initialize distance readings array
  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = 0;
  }
  
  // Display initial message
  lcd.setCursor(0, 0);
  lcd.print("Bottle Counter  ");  // Pad with spaces to clear line
  lcd.setCursor(0, 1);
  lcd.print("Count: 0        ");  // Pad with spaces to clear line
  
  Serial.println("Bottle Counter System Started");
  Serial.println("Initializing sensors...");
  
  // Initialize readings with actual sensor data
  delay(1000);
  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    float distance = getSingleUltrasonicReading();
    if (distance > 0) {
      distanceReadings[i] = distance;
    }
    delay(100);
  }
  readingsInitialized = true;
  
  Serial.println("Sensor initialization complete");
}

void loop() {
  // Read IR sensor for bottle counting
  handleBottleCounting();
  
  // Check container level more frequently for better responsiveness
  checkContainerLevel();
  
  delay(50);  // Reduced delay for better responsiveness
}

void handleBottleCounting() {
  currentIRReading = digitalRead(IR_PIN);
  
  // Print current sensor state for debugging (less frequently)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {  // Print every 2 seconds instead of 500ms
    Serial.print("IR Sensor: ");
    Serial.println(currentIRReading == HIGH ? "HIGH" : "LOW");
    lastPrint = millis();
  }
  
  // Only count on one specific transition and with time delay
  if (currentIRReading == LOW && lastIRReading == HIGH && 
      (millis() - lastCountTime) > COUNT_DELAY) {
    
    bottleCount++;
    lastCountTime = millis();
    
    Serial.print("Bottle detected! Count: ");
    Serial.println(bottleCount);
    
    // Update LCD immediately when count changes
    updateCountDisplay();
  }
  
  lastIRReading = currentIRReading;
}

void checkContainerLevel() {
  // Check level more frequently but still with some interval
  if (millis() - lastLevelCheck < LEVEL_CHECK_INTERVAL) {
    return;
  }
  
  float distance = getStableUltrasonicDistance();
  
  // Print distance for debugging
  Serial.print("Distance reading: ");
  if (distance > 0) {
    Serial.print(distance);
    Serial.print(" cm");
  } else {
    Serial.print("Invalid/No reading");
  }
  
  // Determine if container is full with hysteresis to prevent flickering
  bool isFull;
  if (lastContainerStatus) {
    // If was full, need distance > FULL_THRESHOLD + 1 to become not full
    isFull = (distance <= (FULL_THRESHOLD + 1.0) && distance > 0);
  } else {
    // If was not full, need distance <= FULL_THRESHOLD to become full
    isFull = (distance <= FULL_THRESHOLD && distance > 0);
  }
  
  Serial.print(" - Status: ");
  Serial.println(isFull ? "FULL" : "OK");
  
  // Always update LED based on current status
  if (isFull) {
    digitalWrite(LED_PIN, HIGH);  // Turn on LED when full
    Serial.println("LED ON - Container FULL");
  } else {
    digitalWrite(LED_PIN, LOW);   // Turn off LED when not full
    if (lastContainerStatus) {  // Only print if status changed from full to not full
      Serial.println("LED OFF - Container OK");
    }
  }
  
  // Update display if status changed OR every few cycles
  static int displayUpdateCounter = 0;
  if (isFull != lastContainerStatus || displayUpdateCounter++ > 10) {
    lcd.setCursor(11, 1);  // Position after count
    if (distance > 0) {  // Valid reading
      if (isFull) {
        lcd.print(" FULL");
      } else {
        lcd.print("  OK ");
      }
    } else {
      lcd.print(" ERR ");
      Serial.println("Ultrasonic sensor error - check wiring");
    }
    displayUpdateCounter = 0;
  }
  
  lastContainerStatus = isFull;
  lastLevelCheck = millis();
}

float getStableUltrasonicDistance() {
  // Get new reading
  float newReading = getSingleUltrasonicReading();
  
  if (newReading > 0) {  // Valid reading
    // Store in circular buffer
    distanceReadings[readingIndex] = newReading;
    readingIndex = (readingIndex + 1) % READINGS_TO_AVERAGE;
    
    // Calculate average of valid readings
    float sum = 0;
    int validCount = 0;
    for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
      if (distanceReadings[i] > 0) {
        sum += distanceReadings[i];
        validCount++;
      }
    }
    
    if (validCount > 0) {
      return sum / validCount;
    }
  }
  
  // If no valid readings, return -1
  return -1;
}

float getSingleUltrasonicReading() {
  // Clear trigger pin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Send 10us pulse to trigger
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Read echo pin and calculate distance
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // 30ms timeout
  
  if (duration == 0) {
    return -1;  // Invalid reading
  }
  
  // Calculate distance in cm (speed of sound = 343 m/s)
  float distance = (duration * 0.0343) / 2;
  
  // Filter out obviously wrong readings
  if (distance < 1 || distance > 400) {  // Typical HC-SR04 range is 2-400cm
    return -1;
  }
  
  return distance;
}

void updateCountDisplay() {
  // Only update if count actually changed
  if (bottleCount != lastDisplayedCount) {
    lcd.setCursor(0, 1);
    lcd.print("Count: ");
    lcd.print(bottleCount);
    lcd.print("   ");  // Clear any extra digits
    
    lastDisplayedCount = bottleCount;
  }
}

// Optional: Function to reset counter (you can call this if needed)
void resetCounter() {
  bottleCount = 0;
  lastDisplayedCount = -1;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Counter Reset");
  lcd.setCursor(0, 1);
  lcd.print("Count: 0");
  delay(1000);
  
  // Reset container status
  lastContainerStatus = false;
  digitalWrite(LED_PIN, LOW);
}
