#include <LiquidCrystal_I2C.h>

// Initialize LCD with I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pin definitions
const int IR_PIN = 2;           // IR sensor output pin
const int TRIG_PIN = 9;         // Ultrasonic trigger pin
const int ECHO_PIN = 10;        // Ultrasonic echo pin
const int LED_PIN = 13; 

// Color sensor pins
const int S0_PIN = 4;           // Frequency scaling control
const int S1_PIN = 5;           // Frequency scaling control
const int S2_PIN = 6;           // Color filter control
const int S3_PIN = 7;           // Color filter control
const int OUT_PIN = 8;          // Color sensor output
// OE pin connected to GND for always enabled

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
const unsigned long LEVEL_CHECK_INTERVAL = 500; // Check level every 500ms

// Container parameters
const float CONTAINER_HEIGHT = 20.0;  // Total height in cm
const float FULL_THRESHOLD = 10.0;    // Distance from sensor when full (10cm)

// Variables for stable readings
const int READINGS_TO_AVERAGE = 3;    // Number of readings to average
float distanceReadings[READINGS_TO_AVERAGE];
int readingIndex = 0;
bool readingsInitialized = false;

// Color sensor variables
unsigned long lastColorCheck = 0;
const unsigned long COLOR_CHECK_INTERVAL = 2000; // Check color every 2 seconds
String lastDetectedColor = "NONE";

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
  
  // Initialize color sensor pins
  pinMode(S0_PIN, OUTPUT);
  pinMode(S1_PIN, OUTPUT);
  pinMode(S2_PIN, OUTPUT);
  pinMode(S3_PIN, OUTPUT);
  pinMode(OUT_PIN, INPUT);
  
  // Set frequency scaling to 20% (S0=HIGH, S1=LOW)
  digitalWrite(S0_PIN, HIGH);
  digitalWrite(S1_PIN, LOW);
  
  // Turn off LED initially
  digitalWrite(LED_PIN, LOW);
  
  // Initialize distance readings array
  for (int i = 0; i < READINGS_TO_AVERAGE; i++) {
    distanceReadings[i] = 0;
  }
  
  // Display initial message
  lcd.setCursor(0, 0);
  lcd.print("Bottle Counter  ");
  lcd.setCursor(0, 1);
  lcd.print("Count: 0        ");
  
  Serial.println("Bottle Counter with Color Sensor Started");
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
  
  // Check container level
  checkContainerLevel();
  
  // Check color sensor
  checkColorSensor();
  
  delay(50);  // Small delay for better performance
}

void handleBottleCounting() {
  currentIRReading = digitalRead(IR_PIN);
  
  // Print current sensor state for debugging (less frequently)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 3000) {  // Print every 3 seconds
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
    
    // Also check color when a new bottle is detected
    checkColorSensor();
  }
  
  lastIRReading = currentIRReading;
}

void checkContainerLevel() {
  // Check level with some interval
  if (millis() - lastLevelCheck < LEVEL_CHECK_INTERVAL) {
    return;
  }
  
  float distance = getStableUltrasonicDistance();
  
  // Print distance for debugging
  Serial.print("Distance: ");
  if (distance > 0) {
    Serial.print(distance);
    Serial.print(" cm");
  } else {
    Serial.print("Invalid");
  }
  
  // Determine if container is full with hysteresis
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
  
  // Control LED based on container status
  if (isFull) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  
  // Update display if status changed
  static int displayUpdateCounter = 0;
  if (isFull != lastContainerStatus || displayUpdateCounter++ > 10) {
    updateLevelDisplay(distance, isFull);
    displayUpdateCounter = 0;
  }
  
  lastContainerStatus = isFull;
  lastLevelCheck = millis();
}

void checkColorSensor() {
  // Check color less frequently
  if (millis() - lastColorCheck < COLOR_CHECK_INTERVAL) {
    return;
  }
  
  // Read RGB values
  unsigned long red = getColorReading('R');
  unsigned long green = getColorReading('G');
  unsigned long blue = getColorReading('B');
  unsigned long clear = getColorReading('C');
  
  // Determine color based on RGB values
  String detectedColor = determineColor(red, green, blue, clear);
  
  // Print color information
  Serial.println("=== COLOR SENSOR ===");
  Serial.print("Red: "); Serial.print(red);
  Serial.print(" | Green: "); Serial.print(green);
  Serial.print(" | Blue: "); Serial.print(blue);
  Serial.print(" | Clear: "); Serial.println(clear);
  Serial.print("Detected Color: "); Serial.println(detectedColor);
  Serial.println("==================");
  
  // Update display if color changed
  if (detectedColor != lastDetectedColor) {
    updateColorDisplay(detectedColor);
    lastDetectedColor = detectedColor;
  }
  
  lastColorCheck = millis();
}

unsigned long getColorReading(char color) {
  // Set color filter
  switch(color) {
    case 'R': // Red
      digitalWrite(S2_PIN, LOW);
      digitalWrite(S3_PIN, LOW);
      break;
    case 'G': // Green
      digitalWrite(S2_PIN, HIGH);
      digitalWrite(S3_PIN, HIGH);
      break;
    case 'B': // Blue
      digitalWrite(S2_PIN, LOW);
      digitalWrite(S3_PIN, HIGH);
      break;
    case 'C': // Clear (no filter)
      digitalWrite(S2_PIN, HIGH);
      digitalWrite(S3_PIN, LOW);
      break;
  }
  
  delay(50); // Allow sensor to stabilize
  
  // Read frequency (lower frequency = more of that color detected)
  unsigned long frequency = pulseIn(OUT_PIN, LOW, 50000); // 50ms timeout
  
  return frequency;
}

String determineColor(unsigned long red, unsigned long green, unsigned long blue, unsigned long clear) {
  // Basic color detection logic
  // Lower frequency means more of that color is detected
  
  // Check if readings are valid
  if (red == 0 && green == 0 && blue == 0) {
    return "NO_OBJECT";
  }
  
  // Find the color with the lowest frequency (most detected)
  if (red < green && red < blue) {
    if (red < clear * 0.7) { // Red is significantly lower than clear
      return "RED";
    }
  }
  else if (green < red && green < blue) {
    if (green < clear * 0.7) { // Green is significantly lower than clear
      return "GREEN";
    }
  }
  else if (blue < red && blue < green) {
    if (blue < clear * 0.7) { // Blue is significantly lower than clear
      return "BLUE";
    }
  }
  
  // Check for other colors based on combinations
  if (red < clear * 0.8 && green < clear * 0.8 && blue > clear * 0.9) {
    return "YELLOW";
  }
  else if (red < clear * 0.8 && blue < clear * 0.8 && green > clear * 0.9) {
    return "MAGENTA";
  }
  else if (green < clear * 0.8 && blue < clear * 0.8 && red > clear * 0.9) {
    return "CYAN";
  }
  else if (red < clear * 0.9 && green < clear * 0.9 && blue < clear * 0.9) {
    return "WHITE";
  }
  else if (red > clear * 1.2 && green > clear * 1.2 && blue > clear * 1.2) {
    return "BLACK";
  }
  
  return "UNKNOWN";
}

void updateLevelDisplay(float distance, bool isFull) {
  // Show level status on first line
  lcd.setCursor(0, 0);
  if (distance > 0) {
    if (isFull) {
      lcd.print("CONTAINER: FULL ");
    } else {
      lcd.print("CONTAINER: OK   ");
    }
  } else {
    lcd.print("SENSOR ERROR    ");
  }
}

void updateColorDisplay(String color) {
  // Show color on second line after count
  lcd.setCursor(10, 1);
  if (color == "NO_OBJECT") {
    lcd.print("      ");
  } else if (color.length() <= 6) {
    lcd.print(color);
    // Pad with spaces to clear previous text
    for (int i = color.length(); i < 6; i++) {
      lcd.print(" ");
    }
  }
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
  
  // Calculate distance in cm
  float distance = (duration * 0.0343) / 2;
  
  // Filter out obviously wrong readings
  if (distance < 1 || distance > 400) {
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
    lcd.print(" ");  // Space before color
    
    lastDisplayedCount = bottleCount;
  }
}

// Function to reset counter
void resetCounter() {
  bottleCount = 0;
  lastDisplayedCount = -1;
  lastDetectedColor = "NONE";
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM RESET");
  lcd.setCursor(0, 1);
  lcd.print("Count: 0");
  delay(2000);
  
  // Reset container status
  lastContainerStatus = false;
  digitalWrite(LED_PIN, LOW);
}
