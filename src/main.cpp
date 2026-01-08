/* The purpose of this program is to collect positional data from the PCBMotor TWUM
   Driver. The program can either control the motor through serial communication
   or by PWM. The positional data are sampled at a set interval, stored in a
   queue, and sent through the serial monitor. One core is responsible for
   controlling the motor and data collection while the other is responsible for
   transmitting the positional data. If serial communication is chosen, the driver
   is communicated with via the serial port in the Arduino IDE. If PWM is chosen,
   the motor will operate for a set period of time at the uploaded duty cycle but will
   require a reboot in order to operate again as it is in hardware mode.
*/

/////////////////// Libraries //////////////////
#include <Arduino.h>
#include "SerialDebug.h"

/////////////////// Initializations and Global Variables //////////////////
// Define Debugging
#define DEBUGLEVEL_ERRORS 1
#define DEBUGLEVEL_SAMPLE_TIME 0
#define DEBUGLEVEL_QUEUE_MESSAGES 0
#define SERIAL_DATA 1

// Define Pin Constants
#define RXD2 16               // Serial2 RX Pin
#define TXD2 17               // Serial2 TX Pin          
#define sensor 4              // Sensor Input Pin

// Declare/Initialize Variables
// PWM
const int freq = 50;              // PWM Frequency
const int pwmChannel = 0;         // PWM Channel on ESP32
const int resolution = 5;         // PWM Bit Resolution
float duty = 0;                   // Duty Cycle
float pwmVal = round(duty * 32);  // PWM Value
bool serialCom = false;           // Serial Command or PWM

// Timer
hw_timer_t * timer = NULL;
int preScale = 1000000;
int onTime = 5;
int ticks = preScale * onTime;

// Motor Alignment
bool align = false;           // Motor Align
int i;                        // Sensor reading
String initial;               // Motor Response to 'ap' Command
String initial_Pulse;         // Sensor value from 'ap' Command
String init_Sens;             // Equality for 'ADC' Command based off initial_Pulse

// Data Collection
int Ts = 1000;                // Sample Period (us)
unsigned long temp = 0;       // Collected Data Buffer
int j = 0;                    // Buffer Shift Bit
uint32_t prevTime = 0;        // Sample Time Timer Variable
volatile byte collect = 0;    // Data Collection Flag

// Initialize Handles
TaskHandle_t CommandTask;     // Send Command to Motor
TaskHandle_t PWMTask;         // Send PWM Signal to Motor
TaskHandle_t SerialTask;      // Print Collected Data
QueueHandle_t queue;          // Queue where Data are Stored

/////////////////// Function Definitions //////////////////
void align_Motor();                       // Align Motor to Beginning of Pulse
void clear_RXBuffer();                    // Clear Motor RX Buffer
void interruptTask(void * parameter);     // Task to Create Interrupt on Core0
void command(void * parameter);           // Task to Send Command to Motor
void pwm(void * parameter);               // Task to Send PWM Signal to Motor
void sendData(void * parameter);          // Task to Send Collected Data to Serial Port

/////////////////// Interrupts //////////////////
void IRAM_ATTR turnOff() {
  ledcWrite(pwmChannel, 32);   // Turn PWM Off
}

/////////////////// Setup //////////////////
void setup() {
  // Establish Serial Communication
  Serial2.begin(19200, SERIAL_8N1, RXD2, TXD2);   // Communication with Motor
  Serial.begin(19200);                            // Communication with Serial Port

  // Declare Pins
  pinMode(sensor, INPUT);         // Sensor Pin

  unsigned long alignStart = millis();
  unsigned long alignTimeout = 30000;       // 30 seconds
  // Align Motor to Beginning of Pulse
  while (!align) {
    if((millis() - alignStart) > alignTimeout) {
      serial("ERROR: Motor Alignment Timeout!");  // Timeout
      while(1);
    }
    align_Motor();                  // Call Align Function
    clear_RXBuffer();               // Clear Buffer
    i = digitalRead(sensor);        // Read Current Sensor Output
    Serial2.println("u1-");         // Step Back one uStep
    delay(100);                     // Wait for Command to Complete
    if (i != digitalRead(sensor)) { // Edge Detected if Sensor Reading is Different
      align = true;                 // This is a confirmation for the ADC command in align_Motor()
    }
    Serial2.println("u1");          // Replace uStep
  }

  // Create Queue
  queue = xQueueCreate( 20, sizeof(unsigned long) );
  if (queue == NULL) {
    debugE("ERROR: Queue was not created.");  // Error Message
  }

  // Create Task
  if (serialCom) {
    xTaskCreatePinnedToCore(command, "CommandTask", 1000, NULL, 3, &CommandTask, 1);
  }
  else {
    Serial2.println("t");                                               // Put Motor into Hardware Mode
    delay(100);
    Serial2.end();                                                      // End Serial Communication with Motor
    pinMode(RXD2, OUTPUT);
    pinMode(TXD2, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);                            // Configure PWM
    ledcAttachPin(RXD2, pwmChannel);                                    // Attach PWM channel to Output GPIO
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &turnOff, true);
    timerAlarmWrite(timer, ticks, false);
    xTaskCreatePinnedToCore(pwm, "PWMTask", 1000, NULL, 3, &PWMTask, 1);
  }
}

/////////////////// Custom Functions //////////////////
// Function Aligns Motor to Beginning of Pulse by Comparing Sensor Voltage Readings every uStep
void align_Motor() {
  clear_RXBuffer();                                 // Clear Buffer
  Serial2.println("ap");                            // Request Position Sensor Output
  delay(2000);                                      // Wait for Command to be Processed
  if (Serial2.available() > 0) {                    // Read Reply
    initial = Serial2.readString();
  }
  if (initial.length() < 4) {                       // If Reply is too Short, Return
    return;
  }
  initial_Pulse = initial[initial.length() - 4];    // Get Current Sensor Logic Level
  if (initial_Pulse.toInt() == 0) {                 // If 0, want to uStep until ADC greater than 2100 mV, a logic 1
    init_Sens = ">";
  }
  else {                                            // If 1, want to uStep until ADC less than 2100 mV, a logic 0
    init_Sens = "<";
  }
  //uStep, Read Sensor Output, Compare ADC to 2100, if Condition Met Stop, Repeat 100 Times
  Serial2.println("u1,ap,ADC" + init_Sens + "2100?S,x100");   
}

// Function Clears Input Buffer from Motor
void clear_RXBuffer() {
  Serial2.readString();
}

/////////////////// Main Loop //////////////////
// Collects Data into Buffer and Adds to Queue
void loop() {
  if (((esp_timer_get_time() - prevTime) >= Ts) && collect) {   // Check for Sample Period and Collection Flag
    // Read Register to get Sensor Value, Add it to the buffer MSB -> LSB
    temp |= (((REG_READ(GPIO_IN_REG) & 0b10000) >> 4) << (31 - j)); 
    j++;                                            // Increment Counter
    if (j == 32) {                                  // Send Buffer to Queue Once it has 32 Bits of Information
      if (xQueueSend(queue, &temp, 0) != pdTRUE) {
        // Error Message if Queue is Full and Buffer can't be Added
        serial("ERROR: Reading not placed in queue.");  
      }
      temp = 0;                                     // Reset Buffer
      j = 0;                                        // Reset Counter
    }
    debugS(esp_timer_get_time() - prevTime);        // Print Sample Time
    prevTime = esp_timer_get_time();                // Reset Sample Timer
  }
}


/////////////////// Tasks //////////////////
// Task Sends Command to Motor that was Received in Serial Monitor
void command(void * parameter) {
  for (;;) {
    if ((Serial.available() > 0)) {                // Check if Command was Received
      // Create Data Sending Task
      xTaskCreatePinnedToCore(sendData, "SerialTask", 1000, NULL, 3, &SerialTask, 0);   
      Serial2.println(Serial.readString());        // Send Command to Motor
      collect = 1;                                 // Set Collection Flag
      vTaskSuspend(CommandTask);                   // Suspend this Task to free up CPU
    }
    // Prevent Error from being Thrown
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// Task Sends PWM Signal to Motor that was Received in Serial Monitor
void pwm(void * parameter) {
  for (;;) {
    // Create Data Sending Task
    xTaskCreatePinnedToCore(sendData, "SerialTask", 1000, NULL, 3, &SerialTask, 0);   
    collect = 1;                                    // Set Collection Flag
    ledcWrite(pwmChannel, pwmVal);                  // Write PWM Signal
    REG_WRITE(GPIO_OUT_W1TS_REG, BIT17);            // CW Rotation
    timerAlarmEnable(timer);
    vTaskSuspend(CommandTask);                      // Suspend this Task to free up CPU
    // Prevent Error from being Thrown
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// Tasks Sends Collected Data to Serial Port
void sendData(void * parameter) {
  unsigned long temp1;                               // Data Buffer
  unsigned long tempPrev = esp_random();             // Initialize Previous Reading
  int i = 0;                                         // Counter Variable
  for (;;) {
    debugQ(uxQueueMessagesWaiting(queue));           // Print Number of Messages in the Queue
    // If Message was Received in the Queue, Send it to the Serial Port
    if ((xQueueReceive(queue, &temp1, 0) == pdTRUE)) {    
      serial(temp1);                                 // Print Data
      if (temp1 == tempPrev) {                       // If Message is Repeated, Increment Counter
        i++;
      }
      else {                                         // Assign Previous Message, Reset Counter if Different
        tempPrev = temp1;
        i = 0;
      }
    }
    if (i > 3) {                                     // If Message hasn't Changed for a while:
      collect = 0;                                   // Resets Data Collection Flag
      Serial.println();
      if (serialCom) {
        vTaskResume(CommandTask);                    // Resume Command task and Wait for Command
      }
      vTaskDelete(SerialTask);                       // Delete this Task
      clear_RXBuffer();                              // Clear Serial Buffer
    }
    // Prevent Error from being Thrown
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}
