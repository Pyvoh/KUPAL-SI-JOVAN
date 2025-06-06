#include <Servo.h>
#include <SoftwareSerial.h>

// Software Serial for communication with master Arduino
SoftwareSerial masterSerial(2, 3); // RX=2, TX=3

// MG996R Servo Motor (REJECT ONLY)
Servo rejectServo;
const int SERVO_PIN = 9;
const int SERVO_REJECT_ANGLE = 90;      // 90° for rejected bottles
const int SERVO_DEFAULT_ANGLE = 45;     // 45° as default position

// SG90 Servo Motor (DISPENSE ONLY)
Servo sg90Servo;
const int SG90_PIN = 6;  // Pin for SG90 servo
const int SG90_DISPENSE_ANGLE = 90;  // 90° for dispensing accepted bottles
const int SG90_DEFAULT_ANGLE = 0;    // 0° as default position

// Servo Control Commands (must match master)
#define SERVO_REJECT_CMD 1
#define SERVO_DISPENSE_CMD 2
#define SERVO_RESET_CMD 3
#define SERVO_STATUS_CMD 4
#define SERVO_COUNTER_INCREMENT_CMD 5

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
  
  // Initialize MG996R servo to default 45° position
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  delay(500);
  
  // Initialize SG90 servo to default 0° position
  sg90Servo.attach(SG90_PIN);
  sg90Servo.write(SG90_DEFAULT_ANGLE);
  delay(500);
  
  // Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  Serial.println("Slave Arduino (Dual Servo Controller) Ready");
  Serial.println("MG996R Servo on pin 9 (REJECT ONLY):");
  Serial.println("  - Default: 45° | Reject: 90°");
  Serial.println("SG90 Servo on pin 6 (DISPENSE ONLY):");
  Serial.println("  - Default: 0° | Dispense: 90°");
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
            startServoAction("REJECT");
            currentCommand = SERVO_REJECT_CMD;
          }
          break;
          
        case SERVO_DISPENSE_CMD:
          if (!servoActive && !servoDelaying) {
            startServoAction("DISPENSE");
            currentCommand = SERVO_DISPENSE_CMD;
          }
          break;
          
        case SERVO_COUNTER_INCREMENT_CMD:
          if (!servoActive && !servoDelaying) {
            startServoAction("COUNTER INCREMENT");
            currentCommand = SERVO_COUNTER_INCREMENT_CMD;
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

void startServoAction(String action) {
  servoDelayTime = millis();
  servoDelaying = true;
  servoStatus = 2; // Status: delaying
  
  Serial.print("Starting ");
  Serial.print(action);
  Serial.println(" action (2 second delay)");
  
  if (currentCommand == SERVO_REJECT_CMD) {
    Serial.print("MG996R will move to reject position: ");
    Serial.print(SERVO_REJECT_ANGLE);
    Serial.println("°");
  } else if (currentCommand == SERVO_DISPENSE_CMD) {
    Serial.print("SG90 will move to dispense position: ");
    Serial.print(SG90_DISPENSE_ANGLE);
    Serial.println("°");
  } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
    Serial.println("MG996R will move to 0° for counter increment");
  }
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // Delay completed, activate appropriate servo based on command
    
    if (currentCommand == SERVO_REJECT_CMD) {
      // REJECT: Only MG996R servo moves to 90°
      rejectServo.write(SERVO_REJECT_ANGLE);
      Serial.print("MG996R SERVO ACTIVATED - REJECT | Angle: ");
      Serial.print(SERVO_REJECT_ANGLE);
      Serial.println("° (90° for rejection)");
      Serial.println("SG90 remains at default position (0°)");
      
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      // DISPENSE: Only SG90 servo moves to 90°
      sg90Servo.write(SG90_DISPENSE_ANGLE);
      Serial.print("SG90 SERVO ACTIVATED - DISPENSE | Angle: ");
      Serial.print(SG90_DISPENSE_ANGLE);
      Serial.println("° (90° for dispensing)");
      Serial.println("MG996R remains at default position (45°)");
    } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
      // COUNTER INCREMENT: Only MG996R servo moves to 0°
      rejectServo.write(0);
      Serial.println("MG996R SERVO ACTIVATED - COUNTER INCREMENT | Angle: 0°");
      Serial.println("SG90 remains at default position (0°)");
    }
    
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    servoStatus = 1; // Status: active
  }
  
  // Return servos to default position after active duration
  if (servoActive && (millis() - servoActionTime) >= SERVO_ACTIVE_DURATION) {
    rejectServo.write(SERVO_DEFAULT_ANGLE);  // Return to 45° default
    sg90Servo.write(SG90_DEFAULT_ANGLE);     // Return to 0° default
    servoActive = false;
    servoStatus = 0; // Status: idle
    
    if (currentCommand == SERVO_REJECT_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position (45°)");
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      Serial.println("SG90 SERVO RETURNED to default position (0°)");
    } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position (45°)");
    }
    
    currentCommand = 0;
    Serial.println("Ready for next command");
  }
}

void resetServos() {
  rejectServo.write(SERVO_DEFAULT_ANGLE);  // Reset to 45° default
  sg90Servo.write(SG90_DEFAULT_ANGLE);     // Reset to 0° default
  servoActive = false;
  servoDelaying = false;
  servoStatus = 0;
  currentCommand = 0;
  
  Serial.println("BOTH SERVOS RESET to default positions (MG996R: 45°, SG90: 0°)");
}

void updateStatusLED() {
  if (servoActive || servoDelaying) {
    digitalWrite(STATUS_LED_PIN, HIGH);
  } else {
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
