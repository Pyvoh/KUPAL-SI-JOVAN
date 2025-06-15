#include <Servo.h>
#include <SoftwareSerial.h>

// SoftwareSerial for communication with master Arduino (R3 compatible)
SoftwareSerial masterSerial(2, 3); // RX on pin 2, TX on pin 3

// MG996R Servo Motor (REJECT AND COUNTER INCREMENT)
Servo rejectServo;
const int SERVO_PIN = 9;
const int SERVO_REJECT_ANGLE = 0;       // 0° for rejected bottles
const int SERVO_DEFAULT_ANGLE = 45;     // 45° as default position
const int SERVO_ACCEPT_ANGLE = 90;      // 90° for accepted bottles
const int SERVO_EMPTY_ANGLE = 180;      // 120° for emptying container

// SG90 Servo Motor (3-BOTTLE COMPLETION ONLY)
Servo sg90Servo;
const int SG90_PIN = 6;  // Pin for SG90 servo
const int SG90_DISPENSE_ANGLE = 90;  // 90° for 3-bottle completion only
const int SG90_DEFAULT_ANGLE = 0;    // 0° as default position

// MG995 Servo Motor (3-BOTTLE COMPLETION ONLY - 360° rotation)
Servo mg995Servo;
const int MG995_PIN = 5;  // Pin for MG995 servo
bool mg995Active = false;
unsigned long mg995StartTime = 0;
const unsigned long MG995_ROTATION_DURATION = 1680; // 1.77 seconds for 360° rotation

// Servo Control Commands (must match master)
#define SERVO_REJECT_CMD 1
#define SERVO_DISPENSE_CMD 2          // This is now for 3-bottle completion (activates SG90 and MG995)
#define SERVO_RESET_CMD 3
#define SERVO_STATUS_CMD 4
#define SERVO_COUNTER_INCREMENT_CMD 5 // This only moves MG996R to 90°
#define SERVO_EMPTY_CMD 6             // NEW: This moves MG996R to 120° to empty container

// Servo timing variables
unsigned long servoActionTime = 0;
unsigned long servoDelayTime = 0;
bool servoActive = false;
bool servoDelaying = false;
int currentCommand = 0;
const unsigned long SERVO_DELAY_DURATION = 2000; // 2 seconds delay
const unsigned long SERVO_ACTIVE_DURATION = 4000; // 4 seconds active
const unsigned long SERVO_EMPTY_DURATION = 2000; // 2 seconds for empty command
int servoStatus = 0; // 0 = idle, 1 = active, 2 = delaying

// Status LED (optional - shows servo activity)
const int STATUS_LED_PIN = 13;

void setup() {
  Serial.begin(9600);
  
  // Initialize SoftwareSerial communication for R3
  masterSerial.begin(9600);
  
  // Initialize MG996R servo to default 45° position
  rejectServo.attach(SERVO_PIN);
  rejectServo.write(SERVO_DEFAULT_ANGLE);
  delay(500);
  
  // Initialize SG90 servo to default 0° position
  sg90Servo.attach(SG90_PIN);
  sg90Servo.write(SG90_DEFAULT_ANGLE);
  delay(500);
  
  // MG995 servo starts detached (will only attach when SERVO_DISPENSE_CMD is received)
  // mg995Servo remains detached initially
  
  // Status LED
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  Serial.println("Slave Arduino Ready");
  Serial.println("MG996R: Pin 9");
  Serial.println("- Default: 45, Reject: 0, Accept: 90, Empty: 120");
  Serial.println("SG90: Pin 6 (3-bottle only)");
  Serial.println("- Default: 0, Dispense: 90");
  Serial.println("MG995: Pin 5 (3-bottle only)");
  Serial.println("- 360 rotation then detach");
  Serial.println("SoftwareSerial ready");

  // Test SG90 servo
  Serial.println("Testing SG90...");
  sg90Servo.write(90);
  delay(1000);
  sg90Servo.write(0);
  delay(500);
  Serial.println("SG90 test done");
}

void loop() {
  handleSerialCommunication();
  handleServoMovement();
  handleMG995Servo();
  updateStatusLED();
  delay(100); // Increased delay to reduce processing load
}

void handleSerialCommunication() {
  if (masterSerial.available()) { // Using masterSerial instead of Serial1
    String receivedData = masterSerial.readStringUntil('\n');
    receivedData.trim();
    
    if (receivedData == "STATUS") {
      // Send status back to master
      masterSerial.println(servoStatus); // Using masterSerial instead of Serial1
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
            currentCommand = SERVO_REJECT_CMD;
            startServoAction("REJECT");
          }
          break;
          
        case SERVO_DISPENSE_CMD:
          if (!servoActive && !servoDelaying) {
            currentCommand = SERVO_DISPENSE_CMD;
            startServoAction("3-BOTTLE COMPLETION");
            startMG995Servo();
          }
          break;
          
        case SERVO_COUNTER_INCREMENT_CMD:
          if (!servoActive && !servoDelaying) {
            currentCommand = SERVO_COUNTER_INCREMENT_CMD;
            startServoAction("COUNTER INCREMENT");
          }
          break;
          
        case SERVO_EMPTY_CMD:
          if (!servoActive && !servoDelaying) {
            currentCommand = SERVO_EMPTY_CMD;
            startServoAction("EMPTY CONTAINER");
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
    Serial.print("MG996R will move to accept position: ");
    Serial.print(SERVO_ACCEPT_ANGLE);
    Serial.print("°, SG90 will move to dispense position: ");
    Serial.print(SG90_DISPENSE_ANGLE);
    Serial.println("°, MG995 will perform 360° rotation");
  } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
    Serial.print("MG996R will move to ");
    Serial.print(SERVO_ACCEPT_ANGLE);
    Serial.println("° for counter increment");
  } else if (currentCommand == SERVO_EMPTY_CMD) {
    Serial.print("MG996R will move to empty position: ");
    Serial.print(SERVO_EMPTY_ANGLE);
    Serial.println("° to empty the container");
  }
}

void startMG995Servo() {
  // Attach and start MG995 servo for 360° counter-clockwise rotation
  mg995Servo.attach(MG995_PIN);
  mg995Servo.write(0); // Counter-clockwise rotation (0 = full speed CCW)
  mg995Active = true;
  mg995StartTime = millis();
  Serial.println("MG995 SERVO ATTACHED and starting 360° counter-clockwise rotation");
}

void handleMG995Servo() {
  // Handle MG995 servo 360° rotation and detachment
  if (mg995Active && (millis() - mg995StartTime) >= MG995_ROTATION_DURATION) {
    mg995Servo.detach();
    mg995Active = false;
    Serial.println("MG995 SERVO completed 360° rotation and DETACHED");
  }
}

void handleServoMovement() {
  // Handle delay before servo activation
  if (servoDelaying && (millis() - servoDelayTime) >= SERVO_DELAY_DURATION) {
    // Delay completed, activate appropriate servo based on command
    
    if (currentCommand == SERVO_REJECT_CMD) {
      // REJECT: Only MG996R servo moves to 0°
      rejectServo.write(SERVO_REJECT_ANGLE);
      Serial.print("MG996R SERVO ACTIVATED - REJECT | Angle: ");
      Serial.print(SERVO_REJECT_ANGLE);
      Serial.println("° (0° for rejection)");
      Serial.println("SG90 remains at default position (0°)");
      
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      // 3-BOTTLE COMPLETION: Both servos move (MG996R to 90°, SG90 to 90°)
      Serial.println("DEBUG: About to move both servos for 3-bottle completion");
      rejectServo.write(SERVO_ACCEPT_ANGLE); // Accept position
      delay(100); // Small delay between servo commands
      sg90Servo.write(SG90_DISPENSE_ANGLE);
      Serial.println("BOTH SERVOS ACTIVATED - 3-BOTTLE COMPLETION");
      Serial.print("MG996R: ");
      Serial.print(SERVO_ACCEPT_ANGLE);
      Serial.print("° (accept position), SG90: ");
      Serial.print(SG90_DISPENSE_ANGLE);
      Serial.println("° (dispensing)");
      Serial.println("DEBUG: SG90 command sent successfully");
      
    } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
      // COUNTER INCREMENT: Only MG996R servo moves to 90°
      rejectServo.write(SERVO_ACCEPT_ANGLE);
      Serial.print("MG996R SERVO ACTIVATED - COUNTER INCREMENT | Angle: ");
      Serial.print(SERVO_ACCEPT_ANGLE);
      Serial.println("°");
      Serial.println("SG90 remains at default position (0°)");
      
    } else if (currentCommand == SERVO_EMPTY_CMD) {
      // EMPTY CONTAINER: Only MG996R servo moves to 120°
      rejectServo.write(SERVO_EMPTY_ANGLE);
      Serial.print("MG996R SERVO ACTIVATED - EMPTY CONTAINER | Angle: ");
      Serial.print(SERVO_EMPTY_ANGLE);
      Serial.println("° (120° to empty the container)");
      Serial.println("SG90 remains at default position (0°)");
    }
    
    servoActionTime = millis();
    servoActive = true;
    servoDelaying = false;
    servoStatus = 1; // Status: active
  }
  
  // Return servos to default position after active duration
  // Use shorter duration for SERVO_EMPTY_CMD (2 seconds instead of 4)
  unsigned long activeDuration = (currentCommand == SERVO_EMPTY_CMD) ? SERVO_EMPTY_DURATION : SERVO_ACTIVE_DURATION;
  
  if (servoActive && (millis() - servoActionTime) >= activeDuration) {
    rejectServo.write(SERVO_DEFAULT_ANGLE);  // Return to 45° default
    sg90Servo.write(SG90_DEFAULT_ANGLE);     // Return to 0° default
    servoActive = false;
    servoStatus = 0; // Status: idle
    
    if (currentCommand == SERVO_REJECT_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position (45°)");
    } else if (currentCommand == SERVO_DISPENSE_CMD) {
      Serial.println("BOTH SERVOS RETURNED to default positions");
    } else if (currentCommand == SERVO_COUNTER_INCREMENT_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position (45°)");
    } else if (currentCommand == SERVO_EMPTY_CMD) {
      Serial.println("MG996R SERVO RETURNED to default position (45°) - Container emptied");
    }
    
    currentCommand = 0;
    Serial.println("Ready for next command");
  }
}

void resetServos() {
  rejectServo.write(SERVO_DEFAULT_ANGLE);  // Reset to 45° default
  sg90Servo.write(SG90_DEFAULT_ANGLE);     // Reset to 0° default
  
  // Reset MG995 servo if it's active
  if (mg995Active) {
    mg995Servo.detach();
    mg995Active = false;
    Serial.println("MG995 SERVO DETACHED during reset");
  }
  
  servoActive = false;
  servoDelaying = false;
  servoStatus = 0;
  currentCommand = 0;
  
  Serial.println("ALL SERVOS RESET");
}

void updateStatusLED() {
  if (servoActive || servoDelaying || mg995Active) {
    digitalWrite(STATUS_LED_PIN, HIGH);
  } else {
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
