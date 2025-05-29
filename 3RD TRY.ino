#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Initialize LCD with I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pin definitions
const int IR_PIN = 2;           // IR sensor output pin
const int TRIG_PIN = 9;         // Ultrasonic trigger pin
const int ECHO_PIN = 10;        // Ultrasonic echo pin
const int LED_PIN = 7; 

// TCS34725 I2C Address
#define TCS34725_ADDRESS    0x29
#define TCS34725_COMMAND_BIT 0x80

// TCS34725 Register addresses
#define TCS34725_ENABLE     0x00
#define TCS34725_ATIME      0x01
#define TCS34725_CONTROL    0x0F
#define TCS34725_ID         0x12
#define TCS34725_STATUS     0x13
#define TCS34725_CDATAL     0x14
#define TCS34725_CDATAH     0x15
#define TCS34725_RDATAL     0x16
#define TCS34725_RDATAH     0x17
#define TCS34725_GDATAL     0x18
#define TCS34725_GDATAH     0x19
#define TCS34725_BDATAL     0x1A
#define TCS34725_BDATAH     0x1B

// TCS34725 Enable register bits
#define TCS34725_ENABLE_AIEN   0x10
#define TCS34725_ENABLE_WEN    0x08
#define TCS34725_ENABLE_AEN    0x02
#define TCS34725_ENABLE_PON    0x01

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
bool tcs34725Available = false;

void setup() {
  // Initialize serial communication
  Serial.begin(9600);
  
  // Initialize I2C with custom pins
  Wire.begin();
  // Note: For Arduino Uno/Nano, we can't change I2C pins easily
  // If you need A2/A3 as I2C, you'll need to use SoftwareWire library
  // For now, using standard I2C pins (A4=SDA, A5=SCL)
  Serial.println("Warning: Using standard I2C pins A4(SDA) and A5(SCL)");
  Serial.println("Connect TCS34725: SDA->A4, SCL->A5");
  
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
  lcd.print("Bottle Counter  ");
  lcd.setCursor(0, 1);
  lcd.print("Count: 0        ");
  
  Serial.println("Bottle Counter with TCS34725 RGB Sensor Started");
  Serial.println("Initializing sensors...");
  
  // Initialize TCS34725 color sensor
  if (initTCS34725()) {
    Serial.println("TCS34725 sensor initialized successfully!");
    tcs34725Available = true;
  } else {
    Serial.println("TCS34725 sensor not found! Check wiring.");
    tcs34725Available = false;
  }
  
  // Initialize ultrasonic readings
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
  
  // Check color sensor if available
  if (tcs34725Available) {
    checkColorSensor();
  }
  
  delay(50);  // Small delay for better performance
}

bool initTCS34725() {
  // Check if sensor is present
  uint8_t id = readTCS34725Register(TCS34725_ID);
  if (id != 0x44 && id != 0x10) {
    return false; // Sensor not found
  }
  
  // Enable the sensor
  writeTCS34725Register(TCS34725_ENABLE, TCS34725_ENABLE_PON);
  delay(3); // Power on delay
  
  writeTCS34725Register(TCS34725_ENABLE, TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN);
  delay(3); // ADC enable delay
  
  // Set integration time (154ms for good balance of speed/accuracy)
  writeTCS34725Register(TCS34725_ATIME, 0xC0); // 154ms integration time
  
  // Set gain (4x gain for good sensitivity)
  writeTCS34725Register(TCS34725_CONTROL, 0x01); // 4x gain
  
  return true;
}

void writeTCS34725Register(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readTCS34725Register(uint8_t reg) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);
  Wire.endTransmission();
  
  Wire.requestFrom(TCS34725_ADDRESS, 1);
  return Wire.read();
}

uint16_t readTCS34725Register16(uint8_t reg) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(TCS34725_COMMAND_BIT | reg);
  Wire.endTransmission();
  
  Wire.requestFrom(TCS34725_ADDRESS, 2);
  uint16_t value = Wire.read();
  value |= Wire.read() << 8;
  return value;
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
    if (tcs34725Available) {
      checkColorSensor();
    }
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
  
  // Read RGBC values from TCS34725
  uint16_t clear = readTCS34725Register16(TCS34725_CDATAL);
  uint16_t red = readTCS34725Register16(TCS34725_RDATAL);
  uint16_t green = readTCS34725Register16(TCS34725_GDATAL);
  uint16_t blue = readTCS34725Register16(TCS34725_BDATAL);
  
  // Determine color based on RGB values
  String detectedColor = determineColor(red, green, blue, clear);
  
  // Enhanced debugging output
  Serial.println("========= TCS34725 COLOR SENSOR =========");
  Serial.print("Raw Readings -> Red: "); Serial.print(red);
  Serial.print(" | Green: "); Serial.print(green);
  Serial.print(" | Blue: "); Serial.print(blue);
  Serial.print(" | Clear: "); Serial.println(clear);
  
  // Calculate and show ratios
  if (clear > 0) {
    float redRatio = (float)red / clear;
    float greenRatio = (float)green / clear;
    float blueRatio = (float)blue / clear;
    
    Serial.print("Ratios -> Red: "); Serial.print(redRatio, 3);
    Serial.print(" | Green: "); Serial.print(greenRatio, 3);
    Serial.print(" | Blue: "); Serial.print(blueRatio, 3);
    Serial.println();
    
    Serial.print("Detected Color: "); Serial.println(detectedColor);
  } else {
    Serial.println("Clear reading is 0 - check sensor or lighting!");
  }
  Serial.println("=========================================");
  
  // Update display if color changed
  if (detectedColor != lastDetectedColor) {
    updateColorDisplay(detectedColor);
    lastDetectedColor = detectedColor;
  }
  
  lastColorCheck = millis();
}

String determineColor(uint16_t red, uint16_t green, uint16_t blue, uint16_t clear) {
  // TCS34725 color detection logic
  
  // Check if readings are valid
  if (clear < 10) {
    return "DARK";
  }
  
  // Calculate ratios for better color detection
  float redRatio = (float)red / clear;
  float greenRatio = (float)green / clear;
  float blueRatio = (float)blue / clear;
  
  // Find the dominant color
  float maxRatio = max(redRatio, max(greenRatio, blueRatio));
  float minRatio = min(redRatio, min(greenRatio, blueRatio));
  float contrast = maxRatio - minRatio;
  
  // If all ratios are similar, it's likely white/gray
  if (contrast < 0.05) {
    if (clear > 1000) {
      return "WHITE";
    } else if (clear > 100) {
      return "GRAY";
    } else {
      return "BLACK";
    }
  }
  
  // Determine color based on dominant component
  if (redRatio > 0.35 && redRatio > greenRatio * 1.2 && redRatio > blueRatio * 1.2) {
    return "RED";
  }
  else if (greenRatio > 0.35 && greenRatio > redRatio * 1.2 && greenRatio > blueRatio * 1.2) {
    return "GREEN";
  }
  else if (blueRatio > 0.35 && blueRatio > redRatio * 1.2 && blueRatio > greenRatio * 1.2) {
    return "BLUE";
  }
  else if (redRatio > 0.25 && greenRatio > 0.25 && redRatio > blueRatio * 1.5 && greenRatio > blueRatio * 1.5) {
    return "YELLOW";
  }
  else if (redRatio > 0.25 && blueRatio > 0.25 && redRatio > greenRatio * 1.5 && blueRatio > greenRatio * 1.3) {
    return "MAGENTA";
  }
  else if (greenRatio > 0.25 && blueRatio > 0.25 && greenRatio > redRatio * 1.3 && blueRatio > redRatio * 1.3) {
    return "CYAN";
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
  if (color == "DARK" || color == "NONE") {
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
