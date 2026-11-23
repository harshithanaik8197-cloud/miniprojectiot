/*! *********************************************************************************
 * \addtogroup Main
 * @{
 ********************************************************************************** */
/*! *********************************************************************************
* Copyright 2021-2023 NXP
*
*
* \file
*
* This is the source file for the main entry point for a bare-metal application.
*
* SPDX-License-Identifier: BSD-3-Clause
********************************************************************************** */

/************************************************************************************
 *************************************************************************************
 * Include
 *************************************************************************************
 ************************************************************************************/
#include "app.h"
#include "app_conn.h"
#include "fsl_os_abstraction.h"
#include "mfrc522.h"
#include <stdio.h>
#include <string.h>

#if defined(gAppLowpowerEnabled_d) && (gAppLowpowerEnabled_d > 0)
#include "PWR_Interface.h"
#endif

/************************************************************************************
*************************************************************************************
* Public functions
*************************************************************************************
************************************************************************************/
int main(void)
{
    /* Init OSA: should be called before any other OSA API */
    OSA_Init();

    BOARD_InitHardware();

    /* Start Application services (timers, serial manager, low power, led, button, etc..) */
    APP_InitServices();

    /* Start BLE Platform related ressources such as clocks, Link layer and HCI transport to Link Layer */
    (void)APP_InitBle();

    /* Example of baremetal loop if user doesn't want to use OSA API */
#if (FSL_OSA_BM_TIMER_CONFIG != FSL_OSA_BM_TIMER_NONE)
    OSA_TimeInit();
#endif

    /* Start Host stack */
    BluetoothLEHost_AppInit();

    /* Initialize MFRC522 RFID reader over LPSPI1 (PTB1/2/3, CS=PTA18, RST=PTC4) */
    int mfrcStatus = MFRC522_Init();
    uint8_t mfrcVersion = MFRC522_GetVersion();
    char logBuffer[180];

    if (mfrcStatus == MFRC522_STATUS_OK)
    {
        (void)snprintf(logBuffer, sizeof(logBuffer),
                       "\r\n======================================================\r\n"
                       " [MFRC522] RFID Reader Initialized Successfully!\r\n"
                       " [MFRC522] Chip Version: 0x%02X | Status: READY\r\n"
                       " [MFRC522] Bring an RFID card close to the reader...\r\n"
                       "======================================================\r\n",
                       mfrcVersion);
    }
    else
    {
        (void)snprintf(logBuffer, sizeof(logBuffer),
                       "\r\n[MFRC522] NOTE: Chip init check returned status %d (Version: 0x%02X).\r\n"
                       "[MFRC522] Pins: SCK=PTB2, MOSI=PTB3, MISO=PTB1, CS=PTA18, RST=PTC4\r\n",
                       mfrcStatus, mfrcVersion);
    }
    MFRC522_SerialPrint(logBuffer);

    static uint8_t s_lastUid[MFRC522_UID_SIZE] = {0};
    static bool s_cardPresent = false;
    static uint32_t s_lastPollTime = 0;

    while(TRUE)
    {
        OSA_ProcessTasks();
        BluetoothLEHost_HandleMessages();

        /* Before executing WFI, need to execute some connectivity background tasks
            (usually done in Idle thread) such as NVM save in Idle, etc.. */
        BluetoothLEHost_ProcessIdleTask();

        /* Periodic RFID card detection and UID reporting */
        uint32_t currentTime = OSA_TimeGetMsec();
        if ((currentTime - s_lastPollTime) >= 100U) /* Poll every 100 ms */
        {
            uint8_t currentUid[MFRC522_UID_SIZE] = {0};
            s_lastPollTime = currentTime;

            if (MFRC522_CheckCard(currentUid))
            {
                if (!s_cardPresent || (memcmp(currentUid, s_lastUid, MFRC522_UID_SIZE) != 0))
                {
                    s_cardPresent = true;
                    memcpy(s_lastUid, currentUid, MFRC522_UID_SIZE);

                    (void)snprintf(logBuffer, sizeof(logBuffer),
                                   "\r\n>>> [RFID CARD DETECTED] UID: %02X %02X %02X %02X (0x%02X%02X%02X%02X) <<<\r\n",
                                   currentUid[0], currentUid[1], currentUid[2], currentUid[3],
                                   currentUid[0], currentUid[1], currentUid[2], currentUid[3]);
                    MFRC522_SerialPrint(logBuffer);
                }
            }
            else
            {
                s_cardPresent = false;
            }
        }

        OSA_DisableIRQGlobal();

        /* Check if some connectivity tasks have turned to ready state from interrupts or
              if messages are to be processed in Application process */
        if (( OSA_TaskShouldYield() == FALSE ) && ( BluetoothLEHost_IsMessagePending() == FALSE ) && (BluetoothLEHost_IsConnectivityTaskToProcess() == FALSE))
        {
#if defined(gAppLowpowerEnabled_d) && (gAppLowpowerEnabled_d > 0)
            (void)PWR_EnterLowPower(0U);
#else
            __WFI();
#endif
        }

        OSA_EnableIRQGlobal();
    }

    /* Won't run here */
    assert(0);
    return 0;
}

/*! *********************************************************************************
 * @}
 ********************************************************************************** */
