#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global shared resource
int sharedCounter = 0;
int finished{0};
const int TASKS{10};
const int LOOPS{1000};

// Mutex handle
SemaphoreHandle_t counterMutex;


// Task Declarations
void incrementTaskA(void *pvParameters);
void incrementTaskB(void *pvParameters);

void setup() {

  int i;

  randomSeed(analogRead(A0));

  Serial.begin(9600);
  delay(1000);
  Serial.println("--- ESP32 Mutex Demonstration Start ---");

  // 1. Create the Mutex
  counterMutex = xSemaphoreCreateMutex();

  char PNames[] = {65, 66, 67, 68};
  if (counterMutex != NULL) {
    for(i = 0; i < TASKS; ++i){

        static char taskName[] = "TASK_  *";
        taskName[5] = static_cast<char>(65+i);
         
        char * data = static_cast<char*>(pvPortMalloc(sizeof(char)));
        *data = static_cast<char>(65 + i);
        
        xTaskCreatePinnedToCore(
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
  vTaskDelay(pdMS_TO_TICKS(1000));
  if (finished == TASKS) {
      Serial.println("All tasks Done!");
      Serial.print(" counter = ");
      Serial.println(sharedCounter);
      Serial.print("Should be ");
      Serial.println(TASKS * LOOPS * 100);
      finished = 0;
  }
}

void incrementTask(void *pvParameters) {

  TickType_t delay;
  char *letterPtr = (char *) pvParameters;
  char letter{*letterPtr};
  int i{0};
  for(i =0; i < LOOPS; ++i){
    for(int j = 0; j < 100; ++j){
       sharedCounter++;
    }
    delay = random(1,5);
    vTaskDelay(delay); 
  }
 //lock, increment, unlock
  while (xSemaphoreTake(counterMutex, portMAX_DELAY) != pdTRUE) {
    Serial.println('x');
  }
  ++finished;
  Serial.print(letter);
  Serial.println(" All Finished");
  xSemaphoreGive(counterMutex);
  while(1) {
    vTaskDelay(100);
  }
}