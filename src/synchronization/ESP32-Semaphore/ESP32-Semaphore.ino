#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global shared resource
int sharedCounter = 0;

// Mutex handle
SemaphoreHandle_t counterMutex;


// Task Declarations
void incrementTask(void *pvParameters);

void setup() {

  int i;

  randomSeed(analogRead(A0));//read analog port 0

  Serial.begin(9600);
  delay(1000);
  Serial.println("--- ESP32 Mutex Demonstration Start ---");

  // 1. Create the Mutex
  counterMutex = xSemaphoreCreateMutex();

  char PNames[] = {65, 66, 67, 68}; //ascii
  if (counterMutex != NULL) {
    for(i = 0; i < 4; ++i){

        static char taskName[] = "TASK_  *";
        taskName[5] = PNames[i];
         
        char * data = static_cast<char*>(pvPortMalloc(sizeof(char))); //new character, space off heap
        *data = PNames[i];
        
        xTaskCreatePinnedToCore( //fork
          incrementTask,     // Function to implement the task
          taskName,           // Name of the task
          2048,               // Stack size in words
          data,               // Task input parameter
          1,                  // Priority of the task
          NULL,               // Task handle
          tskNO_AFFINITY      // Core ID: Scheduler assign
        );
    }
  } else {
    Serial.println("Failed to create Mutex!");
  }
}

void loop() {
  // The main loop can remain empty or do other background work
  vTaskDelay(pdMS_TO_TICKS(1000)); //rtos
}

void incrementTask(void *pvParameters) {
 
  char *letterPtr = (char *) pvParameters;
  char letter{*letterPtr};  //parameter
  int delay;

  while (1) {
    // Try to take the mutex. Wait indefinitely (portMAX_DELAY) until available.
    if (xSemaphoreTake(counterMutex, portMAX_DELAY) == pdTRUE) {
      
      // --- CRITICAL SECTION START ---
      Serial.print("[Task ");
      Serial.print(letter);
      Serial.print("] Lock acquired. Counter before: ");
      Serial.println(sharedCounter);
      
      sharedCounter++;

      delay = random(500,10000);
      vTaskDelay(pdMS_TO_TICKS(100)); // Simulate processing delay inside critical section
      
      Serial.print("\t[Task ");
      Serial.print(letter);
      Serial.print("]");
      Serial.print(" Will Delay ");
      Serial.print(delay);
      Serial.print(" Counter after: ");
      Serial.println(sharedCounter);
      // --- CRITICAL SECTION END ---

      // Always release the mutex immediately after the critical section
      xSemaphoreGive(counterMutex);
    }
    
    // Yield execution to allow other tasks to grab the mutex
    vTaskDelay(pdMS_TO_TICKS(delay));
   }
}