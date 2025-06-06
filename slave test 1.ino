#include <Servo.h>
#include <SoftwareSerial.h>

// Software Serial for communication with master Arduino
SoftwareSerial masterSerial(2, 3); // RX=2, TX=3

// MG996R Servo Motor
Servo rejectServo;
const int SERVO_PIN = 9;  // Changed from 3 to 9 (since 3 is used for SoftwareSerial)
const int SERVO_REJECT_ANGLE = 60;
const int SERVO_DISPENSE_ANGLE = 90;  // Different angle for dispensing
const int SERVO_DEFAULT_ANGLE = 0;

// Servo Control Commands (must match master)
#define SERVO_REJECT_CMD 1
#define SERVO_DISPENSE_CMD 2
#define SERVO_RESET_CMD 3
#define SERVO_STATUS_CMD 4

// Servo timing variables
unsigned long servoActionTime = 0;
unsigned long servoDelayTime = 0;
bool servoActive = false;
bool servoDelaying = false;
int currentCommand = 0;
const unsigned long SERVO_DELAY_DURATION = 2000; // 2 seconds delay
const unsigned long SERVO_ACTIVE_DURATION = 4000; // 4 seconds active
int servoStatus = 0; // 0 = idle, 1 = active, 2 = delaying

// Status LED (optional - shows servo activity)
const int STATUS_LED_PIN = 13;

void setup() {
  Serial.begin(9600);
  
  // Initialize Software Serial communication
  masterSerial.begin(9600);
  
  // Initialize servo
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  delay(500);
  
  // Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  Serial.println("Slave Arduino (Servo Controller) Ready");
  Serial.println("Using SoftwareSerial communication");
}

void loop() {
  handleSerialCommunication();
  handleServoMovement();
  updateStatusLED();
  delay(50);
}

void handleSerialCommunication() {
  if (masterSerial.available()) {
    String receivedData = masterSerial.readStringUntil('\n');
    receivedData.trim();
    
    if (receivedData == "STATUS") {
      // Send status back to master
      masterSerial.println(servoStatus);
      Serial.print("Status requested - Sending: ");
      Serial.println(servoStatus);
    } else {
      // Parse command
      int command = receivedData.toInt();
      
      Serial.print("Received command: ");
      Serial.println(command);
      
      switch (command) {
        case SERVO_REJECT_CMD:
          if (!servoActive && !servoDelaying) {
            startServoAction(SERVO_REJECT_ANGLE, "REJECT");
            currentCommand = SERVO_REJECT_CMD;
          }
          break;
          
        case SERVO_DISPENSE_CMD:
          if (!servoActive && !servoDelaying) {
            startServoAction(SERVO_DISPENSE_ANGLE, "DISPENSE");
            currentCommand = SERVO_DISPENSE_CMD;
          }
          break;
          
        case SERVO_RESET_CMD:
          resetServo();
          break;
          
        default:
          Serial.print("Unknown command: ");
          Serial.println(command);
          break;
      }
    }
  }
}

void startServoAction(int angle, String action) {
  servoDelayTime = millis();
  servoDelaying = true;
  servoStatus = 2; // Status: delaying
  
  Serial.print("Starting ");
  Serial.print(action);
  Serial.print(" action - Angle: ");
  Serial.print(angle);
  Serial.println(" (2 second delay)");
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // Delay completed, activate servo
    int targetAngle;
    String action;
    
    if (currentCommand == SERVO_REJECT_CMD) {
      targetAngle = SERVO_REJECT_ANGLE;
      action = "REJECT";
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      targetAngle = SERVO_DISPENSE_ANGLE;
      action = "DISPENSE";
    } else {
      targetAngle = SERVO_DEFAULT_ANGLE;
      action = "DEFAULT";
    }
    
    rejectServo.write(targetAngle);
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    servoStatus = 1; // Status: active
    
    Serial.print("SERVO ACTIVATED - ");
    Serial.print(action);
    Serial.print(" at ");
    Serial.print(targetAngle);
    Serial.println(" degrees");
  }
  
  // Return servo to default position after active duration
  if (servoActive && (millis() - servoActionTime) >= SERVO_ACTIVE_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE);
    servoActive = false;
    servoStatus = 0; // Status: idle
    currentCommand = 0;
    
    Serial.println("SERVO RETURNED to default position");
    Serial.println("Ready for next command");
  }
}

void resetServo() {
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  servoActive = false;
  servoDelaying = false;
  servoStatus = 0;
  currentCommand = 0;
  
  Serial.println("SERVO RESET to default position");
}

void updateStatusLED() {
  // LED indicates servo activity
  if (servoActive || servoDelaying) {
    // Blink LED when servo is active or delaying
    if (millis() % 500 < 250) {
      digitalWrite(STATUS_LED_PIN, HIGH);
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
    }
  } else {
    // LED off when idle
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
