/* This header file allows one to turn specific serial communications ON/OFF to make
   debugging easier. Below are the relvant serial messages:
   DEBUGLEVEL_ERRORS           - Errors associated with FreeRTOS
   DEBUGLEVEL_SAMPLE_TIME      - Debugging the Sensor Sample Time
   DEBUGLEVEL_QUEUE_MESSAGES   - Debugging Message Waiting in the Queue
   SERIAL_DATA                 - Prining Collected Data
*/

void debugNothing(...) {
  // Do Nothing
}

// Turn Serial Prints ON or OFF
#if DEBUGLEVEL_ERRORS == 1
#define debugE(x) Serial.println(x)    
#else
#define debugE(x)    
#endif

#if DEBUGLEVEL_SAMPLE_TIME == 1
#define debugS(x) Serial.println(x)     
#else
#define debugS(x)     
#endif

#if DEBUGLEVEL_QUEUE_MESSAGES == 1
#define debugQ(x) Serial.println(x)    
#else
#define debugQ(x)     
#endif

#if SERIAL_DATA == 1
#define serial(x) Serial.println(x,BIN)   
#else
#define serial(x) 
#endif
