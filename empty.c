/*
 * Copyright (c) 2015-2019, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== empty.c ========
 */

/* For usleep() */
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
// #include <ti/drivers/I2C.h>
// #include <ti/drivers/SPI.h>
// #include <ti/drivers/Watchdog.h>


/* Driver configuration */
#include "ti_drivers_config.h"

 /* POSIX Header files */
  #include <pthread.h>

 /* RTOS header files */
  #include <FreeRTOS.h>
  #include <task.h>

  /* Stack size in bytes */
  #define THREADSTACKSIZE 1024

extern void IncrementGatt(uint_least8_t idx);
extern void DecrementGatt(uint_least8_t idx);

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    /* 1 second delay */
    uint32_t time = 1;

    /* Call driver init functions */
    GPIO_init();
    // I2C_init();
    // SPI_init();
    // Watchdog_init();

    /* Configure the LED pin */
    GPIO_setConfig(CONFIG_GPIO_LED_RED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    /* Turn on user LED */
    GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_GPIO_LED_ON);

    /* Install Button callback */
    GPIO_setCallback(CONFIG_GPIO_Increment, IncrementGatt); //Add this Line!
    //GPIO_setCallback(CONFIG_GPIO_Decrement, DecrementGatt); //Add this Line!
  
    /* Enable interrupts */
    GPIO_enableInt(CONFIG_GPIO_Increment); //Add this Line!
    //GPIO_enableInt(CONFIG_GPIO_Decrement); //Add this Line!

    while (1)
    {
        sleep(time);
        //GPIO_toggle(CONFIG_GPIO_LED_RED);
    }
}

void emptyMain(void)
{
    pthread_t thread;
    pthread_attr_t attrs;
    struct sched_param priParam;
    int retc;

    /* Initialize the attributes structure with default values */
    pthread_attr_init(&attrs);

    /* Set priority, detach state, and stack size attributes */
    priParam.sched_priority = 5; // Lower the priority of this task
    retc = pthread_attr_setschedparam(&attrs, &priParam);
    retc |= pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    retc |= pthread_attr_setstacksize(&attrs, THREADSTACKSIZE);
    if (retc != 0)
    {
        /* failed to set attributes */
        while (1) {}
    }

    retc = pthread_create(&thread, &attrs, mainThread, NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1) {}
    }
}


/*********************************************************************
 * @fn      BLEConnectionEstablished
 *
 * @brief   Called when a Bluetooth connection has been established
 *          with a peer device.
 *
 * @param   None
 *
 * @return  None.
 */
void BLEConnectionEstablished(void)
{
  // Indicate connection by turning on the green LED
  GPIO_write(CONFIG_GPIO_LED_GREEN, CONFIG_LED_ON);
}


/*********************************************************************
 * @fn      BLEConnectionTerminated
 *
 * @brief   Called when the Bluetooth connection has been terminated.
 *
 * @param   None
 *
 * @return  None.
 */
void BLEConnectionTerminated(void)
{
  // Indicate disconnection by turning off the LED
  GPIO_write(CONFIG_GPIO_LED_GREEN, CONFIG_LED_OFF);
}

/*********************************************************************
 * @fn      evaluateNewCharacteristicValue
 *
 * @brief   Based on the new value of a given characteristic determine
 *          if the LED should be turned off or on.
 *
 * @param   newValue: Value of the characteristic to consider
 *
 * @return  None.
 */
void evaluateNewCharacteristicValue(uint8_t newValue)
{
    uint8_t ali = 0;
// If the new value of the characteristic is 0, then we turn off the red LED
    //if(newValue == 0)
    {
        ali = ali +3;
        ali = ali;

        //GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_LED_OFF);
    }
    //else
    {
        ali = ali +3;
        ali = ali;
        
        //GPIO_write(CONFIG_GPIO_LED_RED, CONFIG_LED_ON);
        GPIO_toggle(CONFIG_GPIO_LED_RED);
    }
}
