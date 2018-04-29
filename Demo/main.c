#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

#include "interrupts.h"
#include "gpio.h"
#include "video.h"

#define A_LED_GPIO 	16
#define B_LED_GPIO 	20
#define C_LED_GPIO 	26

#define A_TASK_DELAY 	1000
#define B_TASK_DELAY 	2000
#define C_TASK_DELAY 	5000

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long TickType_t;
typedef long int32_t;

extern void flushCache();
void testPreempt() {
	int x;
	TickType_t start = xTaskGetTickCount();
	for (x=0; x<25;x++){
		println("No PREEMPTION ALLOWED!!!!!!!!!", GREEN_TEXT);
	}
	TickType_t end = xTaskGetTickCount();
	char str[10];
//	sprintf(str, "%d", 42);
	itoa (end-start,str,10);
	println(str, GREEN_TEXT);
}

void task(int pin, TickType_t delay, int type, TickType_t lastWakeTime) {
	int i = 0;
	while (1) {
		i = i ? 0 : 1;
		SetGpio(pin, i);
		if (type == 1) {
			println("ONE", BLUE_TEXT);
		}
		if (type == 2) {
			println("TWO", MAG_TEXT);
		}
		if (type == 3) {
			TickType_t start = xTaskGetTickCount();
			SetGpio(A_LED_GPIO, 1);
			flushCache();
			SetGpio(A_LED_GPIO, 0);
			TickType_t end = xTaskGetTickCount();
			char str[10];
			//print out elapsedtime in ticks
			itoa (end-start,str,10);
			println(str, GREEN_TEXT);
			println("THREE", RED_TEXT);
			testPreempt();
		}
		vTaskDelayUntil(&lastWakeTime, delay);

	}
}

void taskA() {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	task(A_LED_GPIO, A_TASK_DELAY, 1, xLastWakeTime);
	//taskYIELD();
}

void taskB() {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	task(B_LED_GPIO, B_TASK_DELAY, 2, xLastWakeTime);
	//taskYIELD();
}

void taskC() {
	int i = 0;
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	while(1) {
		vTaskDelayUntil(&xLastWakeTime, C_TASK_DELAY);
		makeUnpreemptive();
		testPreempt();
		i = i ? 0 : 1;
		SetGpio(C_LED_GPIO, i);
		//task(C_LED_GPIO, C_TASK_DELAY, 3, xLastWakeTime);
		TickType_t start = xTaskGetTickCount();
		//SetGpio(A_LED_GPIO, 1);
		flushCache();
		//SetGpio(A_LED_GPIO, 0);
		TickType_t end = xTaskGetTickCount();
		char str[10];
		//print out elapsedtime in ticks
		itoa (end-start,str,10);
		println(str, GREEN_TEXT);
		println("THREE", RED_TEXT);
		makePreemptive();
		taskYIELD();
	}
}



int main(void) {
	SetGpioFunction(A_LED_GPIO, 1);
	SetGpioFunction(B_LED_GPIO, 1);
	SetGpioFunction(C_LED_GPIO, 1);

	initFB();

	SetGpio(A_LED_GPIO, 1);
	SetGpio(B_LED_GPIO, 1);
	SetGpio(C_LED_GPIO, 1);

	DisableInterrupts();
	InitInterruptController();

	xTaskCreate(taskA, "LED_A", configMINIMAL_STACK_SIZE, NULL, 0, NULL, 1);
	xTaskCreate(taskB, "LED_B", configMINIMAL_STACK_SIZE, NULL, 0, NULL, 1);
	xTaskCreate(taskC, "LED_C", configMINIMAL_STACK_SIZE, NULL, 0, NULL, 0);

	//set to 0 for no debug, 1 for debug, or 2 for GCC instrumentation (if enabled in config)
	loaded = 1;

	println("Starting task scheduler", ORANGE_TEXT);

	vTaskStartScheduler();

	/*
	 *	We should never get here, but just in case something goes wrong,
	 *	we'll place the CPU into a safe loop.
	 */
	while (1) {
		;
		println("In the forsaken while loop", ORANGE_TEXT);
	}
}

