/*
NoCriticalSection.ino

Race-condition implementation, doesn't protext actual shared work. Spins up 10
FreeRTOS tasks across cores, each task incrementing global sharedCounter 1000000 (1 million) 
times, but increment itself is done unprotected, with no mutex around it. Mutex exists,
but is only used at very end, to safely incremend a finished flag once each task completes.

Once all 10 tasks finish, loop() checks sharedCounter matches the expected total. Because
incrememnts aren't atomic and tasks can preempt each other mid-read-modify-write, actual
count will typically come out lower than expected.
*/

volatile int context_switch_count = 0;

#define traceTASK_SWITCHED_IN() do { \
  context_switch_count++; \
} while(0)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global shared resource
int sharedCounter = 0;

int finished{0};

const int TASKS{10};
const int OUTER_LOOPS{1000};
const int INNER_LOOPS{1000};
const int WORKING_DELAY{10};

// Mutex handle
SemaphoreHandle_t counterMutex;

void incrementTask(void *pvParameters);

void setup() {

  int i;

  randomSeed(analogRead(A0));

  Serial.begin(9600);
  vTaskDelay(2000);
  Serial.println("--- ESP32 Mutex Demonstration Start ---");

  // 1. Create the Mutex
  counterMutex = xSemaphoreCreateMutex();

  if (counterMutex != NULL) {
    for(i = 0; i < TASKS; ++i){

        static char taskName[] = "TASK_ .";
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
          tskNO_AFFINITY   // Core ID: Scheduler assign
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
      Serial.println(TASKS * OUTER_LOOPS * INNER_LOOPS);
      finished = 0;

      if (sharedCounter == TASKS * OUTER_LOOPS * INNER_LOOPS){
        Serial.println("All is good");
      } else {
         Serial.println("ERRORS!!!");
      }

      Serial.print("\nContext Switches: ");
      Serial.println(context_switch_count);
  }
}

#pragma GCC push_options
#pragma GCC optimize ("O0") 
void WhereAmI(char task){
    Serial.print(task);
    Serial.print(" is running on core: ");
    Serial.println(xPortGetCoreID());
}

void incrementTask(void *pvParameters) {

  TickType_t delay;
  char *letterPtr = (char *) pvParameters;
  char letter{*letterPtr};

  WhereAmI(letter);

  for(int i =0; i < OUTER_LOOPS; ++i){
    int a{sharedCounter};
 
    for(int j = 0; j < INNER_LOOPS; ++j){
       //a = sharedCounter;
       //a = a  + 1;
       //sharedCounter = a;
       ++sharedCounter;
    }
    delay = random(1,5);
    vTaskDelay(delay); 
  }

  while (xSemaphoreTake(counterMutex, portMAX_DELAY) != pdTRUE) {
    Serial.println('x');
  }
  ++finished;
  Serial.print(letter);
  Serial.println(" All Finished");
  xSemaphoreGive(counterMutex);
  while(1){
       vTaskDelay(1);
  }
}
#pragma GCC pop_options
