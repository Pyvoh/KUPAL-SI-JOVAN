#include <Servo.h>
#include <SoftwareSerial.h>

// Software Serial for communication with master Arduino
SoftwareSerial masterSerial(2, 3); // RX=2, TX=3

// MG996R Servo Motor (existing)
Servo rejectServo;
const int SERVO_PIN = 9;
const int SERVO_REJECT_ANGLE = 60;
const int SERVO_DISPENSE_ANGLE = 90;
const int SERVO_DEFAULT_ANGLE = 0;

// SG90 Servo Motor (new) - Only for 3-bottle completion
Servo sg90Servo;
const int SG90_PIN = 6;  // Pin for SG90 servo
const int SG90_DISPENSE_ANGLE = 90;  // Only used for 3-bottle completion
const int SG90_DEFAULT_ANGLE = 0;

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
  
  // Initialize MG996R servo
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  delay(500);
  
  // Initialize SG90 servo
  sg90Servo.attach(SG90_PIN);
  sg90Servo.write(SG90_DEFAULT_ANGLE);
  delay(500);
  
  // Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  Serial.println("Slave Arduino (Dual Servo Controller) Ready");
  Serial.println("MG996R Servo on pin 9 (REJECT & DISPENSE)");
  Serial.println("SG90 Servo on pin 6 (3-BOTTLE COMPLETION ONLY)");
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
            startServoAction(SERVO_DISPENSE_ANGLE, "DISPENSE (3-BOTTLE COMPLETION)");
            currentCommand = SERVO_DISPENSE_CMD;
          }
          break;
          
        case SERVO_RESET_CMD:
          resetServos();
          break;
          
        default:
          Serial.print("Unknown command: ");
          Serial.println(command);
          break;
      }
    }
  }
}

void startServoAction(int mg996rAngle, String action) {
  servoDelayTime = millis();
  servoDelaying = true;
  servoStatus = 2; // Status: delaying
  
  Serial.print("Starting ");
  Serial.print(action);
  Serial.print(" action - MG996R: ");
  Serial.print(mg996rAngle);
  Serial.println("° (2 second delay)");
  
  if (currentCommand == SERVO_DISPENSE_CMD) {
    Serial.print("SG90 will also activate for 3-bottle completion: ");
    Serial.print(SG90_DISPENSE_ANGLE);
    Serial.println("°");
  }
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // Delay completed, activate servos based on command
    
    if (currentCommand == SERVO_REJECT_CMD) {
      // REJECT: Only MG996R servo moves
      rejectServo.write(SERVO_REJECT_ANGLE);
      Serial.print("MG996R SERVO ACTIVATED - REJECT | Angle: ");
      Serial.print(SERVO_REJECT_ANGLE);
      Serial.println("°");
      Serial.println("SG90 remains at default position");
      
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      // DISPENSE (3-bottle completion): Both servos move
      rejectServo.write(SERVO_DISPENSE_ANGLE);
      sg90Servo.write(SG90_DISPENSE_ANGLE);
      Serial.print("BOTH SERVOS ACTIVATED - 3 BOTTLES COMPLETED | ");
      Serial.print("MG996R: ");
      Serial.print(SERVO_DISPENSE_ANGLE);
      Serial.print("°, SG90: ");
      Serial.print(SG90_DISPENSE_ANGLE);
      Serial.println("°");
    }
    
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    servoStatus = 1; // Status: active
  }
  
  // Return servos to default position after active duration
  if (servoActive && (millis() - servoActionTime) >= SERVO_ACTIVE_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE);
    sg90Servo.write(SG90_DEFAULT_ANGLE);
    servoActive = false;
    servoStatus = 0; // Status: idle
    
    if (currentCommand == SERVO_REJECT_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position");
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      Serial.println("BOTH SERVOS RETURNED to default positions");
    }
    
    currentCommand = 0;
    Serial.println("Ready for next command");
  }
}

void resetServos() {
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  sg90Servo.write(SG90_DEFAULT_ANGLE);
  servoActive = false;
  servoDelaying = false;
  servoStatus = 0;
  currentCommand = 0;
  
  Serial.println("BOTH SERVOS RESET to default positions");
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
