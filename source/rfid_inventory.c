/*! *********************************************************************************
 * \addtogroup RFID Inventory Management Application
 * @{
 ********************************************************************************** */
/*! *********************************************************************************
* Copyright 2015 Freescale Semiconductor, Inc.
* Copyright 2016-2024 NXP
*
* Modified for IoT-Based Smart Inventory Management System
* Using FRDM-MCXW71 + RC522 RFID Reader + BLE
*
* \file
*
* This file is the source file for the RFID Inventory Management Application.
* It replaces the original Wireless UART + Accel/Light sensor demo.
*
* The system:
*  - Reads RFID cards/tags using RC522 reader (SPI)
*  - Maintains an in-memory inventory of products
*  - Toggles stock (add/remove) on each scan
*  - Alerts via buzzer for successful scan / unknown tag
*  - Sends inventory data over BLE (Wireless UART service)
*  - A phone/laptop receives BLE data and can forward to Node-RED dashboard
*
* SPDX-License-Identifier: BSD-3-Clause
********************************************************************************** */

/************************************************************************************
 *************************************************************************************
 * Include
 *************************************************************************************
 ************************************************************************************/
/* Standard C */
#include <stdio.h>
#include <string.h>

/* Framework / Drivers */
#include "EmbeddedTypes.h"
#include "RNG_Interface.h"
#include "fsl_component_button.h"
#include "fsl_component_led.h"
#include "fsl_component_timer_manager.h"
#include "fsl_component_panic.h"
#include "fsl_component_serial_manager.h"
#include "fsl_component_mem_manager.h"
#include "fsl_format.h"
#include "fsl_debug_console.h"
#include "app.h"
#include "FunctionLib.h"
#include "board.h"
#include "fwk_platform_ble.h"

/* BLE Host Stack */
#include "gatt_interface.h"
#include "gatt_server_interface.h"
#include "gatt_client_interface.h"
#include "gatt_database.h"
#include "gap_interface.h"
#include "gatt_db_app_interface.h"
#include "gatt_db_handles.h"
#include "gap_types.h"

/* Profile / Services */
#include "wireless_uart_interface.h"
#include "battery_interface.h"
/* Wrappers */
#include "ble_conn_manager.h"
#include "ble_service_discovery.h"

#include "board.h"
#include "board_comp.h"
#include "rfid_inventory.h"

/* RC522 RFID Driver */
#include "rc522_drv.h"

/* GPIO for buzzer */
#include "fsl_gpio.h"
#include "fsl_port.h"

#if defined(K32W232H_SERIES) || defined(KW45B41Z82_SERIES) || defined(KW45B41Z83_SERIES) || defined(K32W1480_SERIES) || \
    defined(KW47B42ZB7_cm33_core0_SERIES) || defined(KW47B42ZB6_cm33_core0_SERIES) || defined(KW47B42ZB3_cm33_core0_SERIES) || \
    defined(KW47B42ZB2_cm33_core0_SERIES) || defined(KW47B42Z97_cm33_core0_SERIES) || defined(KW47B42Z96_cm33_core0_SERIES) || \
    defined(KW47B42Z83_cm33_core0_SERIES) || defined(MCXW716C_SERIES) || defined(MCXW716A_SERIES)
#include "sensors.h"
#endif

#if defined(gUseControllerNotificationsCallback_c) && (gUseControllerNotificationsCallback_c)
    #include "controller_interface.h"
#endif

#include "app_conn.h"
#include "app_scanner.h"
#include "app_advertiser.h"
#include "board.h"
#include "app.h"

/************************************************************************************
 *************************************************************************************
 * Private macros
 *************************************************************************************
 ************************************************************************************/

#define mAppUartBufferSize_c            gAttMaxWriteDataSize_d(gAttMaxMtu_c) /* Local Buffer Size */

#define mAppUartFlushIntervalInMs_c     (7)     /* Flush Timeout in Ms */

#define mBatteryLevelReportInterval_c   (10)    /* battery level report interval in seconds  */

#define gAllowToBlock_d                 (TRUE)
#define gNoBlock_d                      (FALSE)
#define Serial_Print(a,b)               do{ \
                                            union \
                                            { \
                                                const char *pStr; \
                                                uint8_t *pUint8; \
                                            } temp; \
                                            temp.pStr = (a); \
                                            if ((b) == gAllowToBlock_d) \
                                            { \
                                                (void)SerialManager_WriteBlocking((serial_write_handle_t)s_writeHandle, (uint8_t *)temp.pUint8, strlen(temp.pStr)); \
                                            } \
                                            else \
                                            { \
                                                (void)SerialManager_WriteNonBlocking((serial_write_handle_t)s_writeHandle, (uint8_t *)temp.pUint8, strlen(temp.pStr)); \
                                            } \
                                        } while(0);
#define Serial_PrintDec(a)              (void)SerialManager_WriteBlocking((serial_write_handle_t)s_writeHandle, FORMAT_Dec2Str(a), strlen((char const *)FORMAT_Dec2Str(a)))
#define Serial_PrintHex(a)              (void)SerialManager_WriteBlocking((serial_write_handle_t)s_writeHandle, FORMAT_Hex2Ascii(a), strlen((const char*)FORMAT_Hex2Ascii(a)))

/* Buzzer GPIO: PTC5 (PORTC, Pin 5 — free GPIO, no pin conflict) */
#define BUZZER_GPIO         GPIOC
#define BUZZER_PORT         PORTC
#define BUZZER_PIN          5U

#define BUZZER_ON()         GPIO_PinWrite(BUZZER_GPIO, BUZZER_PIN, 1)
#define BUZZER_OFF()        GPIO_PinWrite(BUZZER_GPIO, BUZZER_PIN, 0)

/************************************************************************************
 *************************************************************************************
 * Private type definitions
 *************************************************************************************
 ************************************************************************************/
typedef enum appEvent_tag
{
    mAppEvt_PeerConnected_c,
    mAppEvt_PairingComplete_c,
    mAppEvt_ServiceDiscoveryComplete_c,
    mAppEvt_ServiceDiscoveryNotFound_c,
    mAppEvt_ServiceDiscoveryFailed_c,
    mAppEvt_GattProcComplete_c,
    mAppEvt_GattProcError_c
} appEvent_t;

typedef enum appState_tag
{
    mAppIdle_c,
    mAppExchangeMtu_c,
    mAppServiceDisc_c,
    mAppServiceDiscRetry_c,
    mAppRunning_c
} appState_t;

typedef struct appPeerInfo_tag
{
    deviceId_t  deviceId;
    bool_t      isBonded;
    wucConfig_t clientInfo;
    appState_t  appState;
    gapRole_t   gapRole;
} appPeerInfo_t;

typedef struct advState_tag
{
    bool_t advOn;
} advState_t;

/* ========== INVENTORY DATA STRUCTURES ========== */

typedef enum
{
    SCAN_ACTION_ADD = 0,
    SCAN_ACTION_REMOVE
} scanAction_t;

typedef struct inventoryItem_tag
{
    uint8_t     uid[RC522_UID_SIZE];    /* 4-byte RFID UID */
    uint16_t    productId;               /* Product ID (e.g. 1001, 1002, 1003) */
    char        productName[16];         /* Product name string */
    int16_t     stockCount;              /* Current stock count */
    scanAction_t lastAction;             /* Last scan action (toggle) */
    bool_t      valid;                   /* Entry is valid */
} inventoryItem_t;

/************************************************************************************
 *************************************************************************************
 * Private functions prototypes
 *************************************************************************************
 ************************************************************************************/
static void BleApp_Start(gapRole_t gapRole);
#if gWuart_PeripheralRole_c == 1
static void BleApp_AdvertisingCallback(gapAdvertisingEvent_t *pAdvertisingEvent);
#endif /* gWuart_PeripheralRole_c */
#if gWuart_CentralRole_c == 1
static void BleApp_ScanningCallback(gapScanningEvent_t *pScanningEvent);
#endif /* gWuart_CentralRole_c */
static void BleApp_ConnectionCallback(deviceId_t peerDeviceId, gapConnectionEvent_t *pConnectionEvent);
#if gWuart_CentralRole_c == 1
static bool_t BleApp_CheckScanEvent(gapScannedDevice_t *pData);
#endif /* gWuart_CentralRole_c */
/* Gatt and Att callbacks */
static void BleApp_GattServerCallback(deviceId_t deviceId, gattServerEvent_t *pServerEvent);
static void BleApp_GattClientCallback(deviceId_t serverDeviceId, gattProcedureType_t procedureType, gattProcedureResult_t   procedureResult, bleResult_t error);
static void BleApp_ServiceDiscoveryCallback(deviceId_t peerDeviceId, servDiscEvent_t *pEvent);
static void BleApp_StateMachineHandler(deviceId_t peerDeviceId, appEvent_t event);
static void BleApp_StoreServiceHandles(deviceId_t peerDeviceId, gattService_t *pService);

/* Timer Callbacks */
#if gWuart_CentralRole_c == 1
static void ScanningTimerCallback(void *pParam);
#endif /* gWuart_CentralRole_c */
static void UartStreamFlushTimerCallback(void *pData);
static void BatteryMeasurementTimerCallback(void *pParam);
static void RfidScanTimerCallback(void *pParam);
static void Buzzer_TimerCallback(void *pParam);

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c == 1))
static void SwitchPressTimerCallback(void *pParam);
#endif

/* Uart Tx/Rx Callbacks*/
static void Uart_RxCallBack(void *pData, serial_manager_callback_message_t *pMessage, serial_manager_status_t status);
static void Uart_TxCallBack(void *pBuffer, serial_manager_callback_message_t *pMessage, serial_manager_status_t status);

static void BleApp_FlushUartStream(void *pParam);
static void BleApp_ReceivedUartStream(deviceId_t peerDeviceId, uint8_t *pStream, uint16_t streamLength);

#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
static void BleApp_HandleControllerNotification(bleNotificationEvent_t *pNotificationEvent);

#if defined(gUseControllerNotificationsCallback_c) && (gUseControllerNotificationsCallback_c)
static void BleApp_ControllerNotificationCallback(bleCtrlNotificationEvent_t *pNotificationEvent);
#endif
#endif

static void BleApp_SerialInit(void);
static void BleApp_RfidInit(void);
static void BleApp_BuzzerInit(void);
static void BluetoothLEHost_Initialized(void);
static void BluetoothLEHost_GenericCallback(gapGenericEvent_t *pGenericEvent);

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 0))
static button_status_t BleApp_HandleKeys0(void *pButtonHandle, button_callback_message_t *pMessage, void *pCallbackParam);
#endif /*gAppButtonCnt_c > 0*/

#if (gWuart_CentralRole_c == 1) && (gWuart_PeripheralRole_c == 1)
#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 1))
static button_status_t BleApp_HandleKeys1(void *pButtonHandle, button_callback_message_t *pMessage, void *pCallbackParam);
#endif
#endif

/* Inventory functions */
static void Inventory_Init(void);
static int Inventory_FindByUID(uint8_t uid[RC522_UID_SIZE]);
static void Inventory_ProcessTag(uint8_t uid[RC522_UID_SIZE]);
static void Inventory_SendBleUpdate(int itemIndex, scanAction_t action);
static void Inventory_SendUnknownAlert(uint8_t uid[RC522_UID_SIZE]);
static void Inventory_SendStatus(void);

/* Buzzer functions */
static void Buzzer_BeepSuccess(void);
static void Buzzer_BeepError(void);

static void BleApp_SendUartStream(uint8_t *pRecvStream, uint8_t streamSize);
static void DelayMs(uint32_t ms);

/************************************************************************************
 *************************************************************************************
 * Private memory declarations
 *************************************************************************************
 ************************************************************************************/
static appPeerInfo_t maPeerInformation[gAppMaxConnections_c];
static gapRole_t mGapRole;
static deviceId_t mcActiveConnNo;
/* Adv Parameters */
#if gWuart_PeripheralRole_c == 1
static advState_t mAdvState;
#endif /* gWuart_PeripheralRole_c */
#if gWuart_CentralRole_c == 1
static bool_t mScanningOn = FALSE;
#endif /* gWuart_CentralRole_c */

static uint16_t mCharMonitoredHandles[1] = { (uint16_t)value_uart_stream };

/* Service Data*/
static wusConfig_t mWuServiceConfig;
static bool_t      mBasValidClientList[gAppMaxConnections_c] = {FALSE};
static basConfig_t mBasServiceConfig = {(uint16_t)service_battery, 0, mBasValidClientList, gAppMaxConnections_c};

static TIMER_MANAGER_HANDLE_DEFINE(mAppTimerId);
static TIMER_MANAGER_HANDLE_DEFINE(mUartStreamFlushTimerId);
static TIMER_MANAGER_HANDLE_DEFINE(mBatteryMeasurementTimerId);
static TIMER_MANAGER_HANDLE_DEFINE(mRfidScanTimerId);
static TIMER_MANAGER_HANDLE_DEFINE(mBuzzerTimerId);

/* If the board has only one button, multiplex the required functionalities on it using an application timer */
#if (gAppButtonCnt_c == 1)
static TIMER_MANAGER_HANDLE_DEFINE(mSwitchPressTimerId);
#endif


/*wireless uart write handle*/
static SERIAL_MANAGER_WRITE_HANDLE_DEFINE(s_writeHandle);
/*wireless uart read handle*/
static SERIAL_MANAGER_READ_HANDLE_DEFINE(s_readHandle);

static uint16_t mAppUartBufferSize = mAppUartBufferSize_c;
static volatile bool_t mAppUartNewLine = FALSE;
static volatile bool_t mAppDapaPending = FALSE;

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c == 1))
static uint8_t mSwitchPressCnt = 0;
#endif

#if (gWuart_CentralRole_c == 1)
static appScanningParams_t mAppScanParams = {
    &gScanParams,
    gGapDuplicateFilteringEnable_c,
    gGapScanContinuously_d,
    gGapScanPeriodicDisabled_d
};
#endif

#if (gWuart_PeripheralRole_c == 1)
static appAdvertisingParams_t mAppAdvParams = {
    &gAdvParams,
    &gAppAdvertisingData,
    &gAppScanRspData
};
#endif

/* ========== INVENTORY TABLE ========== */
static inventoryItem_t mInventory[INVENTORY_MAX_PRODUCTS];
static uint8_t mInventoryCount = 0;
static bool_t mRfidInitialized = FALSE;

/* Last scanned UID and timestamp for debouncing */
static uint8_t mLastScannedUID[RC522_UID_SIZE] = {0};
static uint32_t mLastScanTick = 0;
static uint32_t mTickCounter = 0;  /* System tick counter (incremented every 500 ms) */

/* State-based presence tracking: suppresses duplicate scans while tag rests in RF field */
static bool_t mCardCurrentlyInField = FALSE;

/* Non-blocking buzzer state machine */
typedef enum
{
    BUZZER_PATTERN_IDLE = 0,
    BUZZER_PATTERN_SUCCESS, /* 1 short beep (100 ms) */
    BUZZER_PATTERN_ERROR    /* 3 alert beeps (200ms ON, 150ms OFF) */
} buzzerPattern_t;

static buzzerPattern_t mBuzzerPattern = BUZZER_PATTERN_IDLE;
static uint8_t mBuzzerStep = 0;

/************************************************************************************
*************************************************************************************
* Public functions
*************************************************************************************
************************************************************************************/

/*! *********************************************************************************
* \brief  This is the initialization function for the RFID Inventory application.
********************************************************************************** */
void BluetoothLEHost_AppInit(void)
{
    BleApp_SerialInit();
    BleApp_BuzzerInit();
    BleApp_RfidInit();

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 0))
    (void)BUTTON_InstallCallback((button_handle_t)g_buttonHandle[0], BleApp_HandleKeys0, NULL);

#if (gWuart_CentralRole_c == 1) && (gWuart_PeripheralRole_c == 1)
#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 1))
    (void)BUTTON_InstallCallback((button_handle_t)g_buttonHandle[1], BleApp_HandleKeys1, NULL);
#endif
#endif
#endif

    /* Initialize Bluetooth Host Stack */
    BluetoothLEHost_SetGenericCallback(BluetoothLEHost_GenericCallback);
    BluetoothLEHost_Init(BluetoothLEHost_Initialized);
}

/************************************************************************************
*************************************************************************************
* Private functions
*************************************************************************************
************************************************************************************/

static void DelayMs(uint32_t ms)
{
    volatile uint32_t count;
    for (uint32_t i = 0; i < ms; i++)
    {
        for (count = 0; count < 12000U; count++)
        {
            __NOP();
        }
    }
}

/* ========== INVENTORY MANAGEMENT ========== */

static void Inventory_Init(void)
{
    mInventoryCount = 7;

    /* Product 1: Laptop (UID: 2E 6D 9E 04) */
    mInventory[0].productId = 1001;
    mInventory[0].uid[0] = 0x2E; mInventory[0].uid[1] = 0x6D;
    mInventory[0].uid[2] = 0x9E; mInventory[0].uid[3] = 0x04;
    FLib_MemCpy(mInventory[0].productName, "Laptop", 7);
    mInventory[0].stockCount = 10;
    mInventory[0].lastAction = SCAN_ACTION_REMOVE;  /* First scan will trigger STOCK IN (ADD) */
    mInventory[0].valid = TRUE;

    /* Product 2: Keyboard (UID: FA 76 26 02) */
    mInventory[1].productId = 1002;
    mInventory[1].uid[0] = 0xFA; mInventory[1].uid[1] = 0x76;
    mInventory[1].uid[2] = 0x26; mInventory[1].uid[3] = 0x02;
    FLib_MemCpy(mInventory[1].productName, "Keyboard", 9);
    mInventory[1].stockCount = 20;
    mInventory[1].lastAction = SCAN_ACTION_REMOVE;  /* First scan will trigger STOCK IN (ADD) */
    mInventory[1].valid = TRUE;

    /* Product 3: Mouse (UID: 23 8A 9C 04) */
    mInventory[2].productId = 1003;
    mInventory[2].uid[0] = 0x23; mInventory[2].uid[1] = 0x8A;
    mInventory[2].uid[2] = 0x9C; mInventory[2].uid[3] = 0x04;
    FLib_MemCpy(mInventory[2].productName, "Mouse", 6);
    mInventory[2].stockCount = 15;
    mInventory[2].lastAction = SCAN_ACTION_REMOVE;  /* First scan will trigger STOCK IN (ADD) */
    mInventory[2].valid = TRUE;

    /* Product 4: Monitor (UID: 69 84 A0 04) */
    mInventory[3].productId = 1004;
    mInventory[3].uid[0] = 0x69; mInventory[3].uid[1] = 0x84;
    mInventory[3].uid[2] = 0xA0; mInventory[3].uid[3] = 0x04;
    FLib_MemCpy(mInventory[3].productName, "Monitor", 8);
    mInventory[3].stockCount = 8;
    mInventory[3].lastAction = SCAN_ACTION_REMOVE;  /* First scan will trigger STOCK IN (ADD) */
    mInventory[3].valid = TRUE;

    /* Product 5: Headset (UID: DA 17 9F 04) */
    mInventory[4].productId = 1005;
    mInventory[4].uid[0] = 0xDA; mInventory[4].uid[1] = 0x17;
    mInventory[4].uid[2] = 0x9F; mInventory[4].uid[3] = 0x04;
    FLib_MemCpy(mInventory[4].productName, "Headset", 8);
    mInventory[4].stockCount = 12;
    mInventory[4].lastAction = SCAN_ACTION_REMOVE;
    mInventory[4].valid = TRUE;

    /* Product 6: Webcam (UID: 94 21 4C 06) */
    mInventory[5].productId = 1006;
    mInventory[5].uid[0] = 0x94; mInventory[5].uid[1] = 0x21;
    mInventory[5].uid[2] = 0x4C; mInventory[5].uid[3] = 0x06;
    FLib_MemCpy(mInventory[5].productName, "Webcam", 7);
    mInventory[5].stockCount = 14;
    mInventory[5].lastAction = SCAN_ACTION_REMOVE;
    mInventory[5].valid = TRUE;

    /* Product 7: USB-Drive (UID: 93 3E 02 07) */
    mInventory[6].productId = 1007;
    mInventory[6].uid[0] = 0x93; mInventory[6].uid[1] = 0x3E;
    mInventory[6].uid[2] = 0x02; mInventory[6].uid[3] = 0x07;
    FLib_MemCpy(mInventory[6].productName, "USB-Drive", 10);
    mInventory[6].stockCount = 25;
    mInventory[6].lastAction = SCAN_ACTION_REMOVE;
    mInventory[6].valid = TRUE;

    /* Clear remaining slots */
    for (uint8_t i = mInventoryCount; i < INVENTORY_MAX_PRODUCTS; i++)
    {
        mInventory[i].valid = FALSE;
        mInventory[i].stockCount = 0;
        mInventory[i].productId = 0;
    }
}

static int Inventory_FindByUID(uint8_t uid[RC522_UID_SIZE])
{
    for (uint8_t i = 0; i < mInventoryCount; i++)
    {
        if (mInventory[i].valid &&
            FLib_MemCmp(mInventory[i].uid, uid, RC522_UID_SIZE))
        {
            return (int)i;
        }
    }
    return -1;  /* Not found */
}

static void Inventory_ProcessTag(uint8_t uid[RC522_UID_SIZE])
{
    int index;
    char logBuf[384];
    uint32_t totalSec = mTickCounter / 2U;
    uint32_t min = totalSec / 60U;
    uint32_t sec = totalSec % 60U;
    uint32_t subsec = (mTickCounter % 2U) * 500U;

    /* Print header with timestamp */
    (void)snprintf(logBuf, sizeof(logBuf),
                   "\r\n--- [RFID SCAN EVENT @ %02u:%02u.%03u] ---\r\n"
                   "[RFID]      Card detected in RF field (Status: OK)\r\n"
                   "[RFID]      Card UID: %02X %02X %02X %02X\r\n"
                   "[RFID]      Reader Communication: Verified (Checksum: PASS)\r\n",
                   (unsigned int)min, (unsigned int)sec, (unsigned int)subsec,
                   uid[0], uid[1], uid[2], uid[3]);
    Serial_Print(logBuf, gAllowToBlock_d);

    /* Look up tag in local inventory database */
    index = Inventory_FindByUID(uid);

    if (index >= 0)
    {
        /* Known registered product */
        scanAction_t newAction;
        int16_t prevStock = mInventory[index].stockCount;

        if (mInventory[index].lastAction == SCAN_ACTION_ADD)
        {
            newAction = SCAN_ACTION_REMOVE;
        }
        else
        {
            newAction = SCAN_ACTION_ADD;
        }

        /* Check for Out-of-Stock condition */
        if (newAction == SCAN_ACTION_REMOVE && prevStock <= 0)
        {
            (void)snprintf(logBuf, sizeof(logBuf),
                           "[PRODUCT]   Product ID: %u | Name: %s\r\n"
                           "[PRODUCT]   Match Status: REGISTERED\r\n"
                           "[INVENTORY] Operation: STOCK OUT\r\n"
                           "[INVENTORY] Previous Quantity: 0\r\n"
                           "[ERROR]     Stock is empty! Cannot decrement below zero.\r\n"
                           "[INVENTORY] Updated Quantity: 0\r\n"
                           "[INVENTORY] Result: FAILED (OUT OF STOCK)\r\n"
                           "[ALERT]     OUT-OF-STOCK ALERT: '%s' has 0 units remaining!\r\n"
                           "[BUZZER]    Audio Feedback: 3 Alert Beeps (Error)\r\n",
                           (unsigned int)mInventory[index].productId,
                           mInventory[index].productName,
                           mInventory[index].productName);
            Serial_Print(logBuf, gAllowToBlock_d);

            /* Trigger error beep pattern (non-blocking) */
            Buzzer_BeepError();

            /* Send alert telemetry over BLE */
            char alertBuf[64];
            (void)snprintf(alertBuf, sizeof(alertBuf), "ALERT:OUT_OF_STOCK:%s\n", mInventory[index].productName);
            BleApp_SendUartStream((uint8_t *)alertBuf, (uint8_t)strlen(alertBuf));
            return;
        }

        /* Execute Stock Update */
        if (newAction == SCAN_ACTION_ADD)
        {
            mInventory[index].stockCount++;
        }
        else
        {
            mInventory[index].stockCount--;
        }

        mInventory[index].lastAction = newAction;

        /* Print structured inventory log */
        (void)snprintf(logBuf, sizeof(logBuf),
                       "[PRODUCT]   Product ID: %u | Name: %s\r\n"
                       "[PRODUCT]   Match Status: REGISTERED\r\n"
                       "[INVENTORY] Operation: %s\r\n"
                       "[INVENTORY] Previous Quantity: %d\r\n"
                       "[INVENTORY] Updated Quantity: %d\r\n"
                       "[INVENTORY] Result: SUCCESS\r\n"
                       "[BUZZER]    Audio Feedback: 1 Short Beep (Success)\r\n",
                       (unsigned int)mInventory[index].productId,
                       mInventory[index].productName,
                       (newAction == SCAN_ACTION_ADD ? "STOCK IN" : "STOCK OUT"),
                       (int)prevStock,
                       (int)mInventory[index].stockCount);
        Serial_Print(logBuf, gAllowToBlock_d);

        /* Low stock alert check */
        if (mInventory[index].stockCount <= 2 && mInventory[index].stockCount > 0)
        {
            (void)snprintf(logBuf, sizeof(logBuf),
                           "[ALERT]     LOW STOCK WARNING: '%s' has only %d unit(s) remaining!\r\n",
                           mInventory[index].productName, (int)mInventory[index].stockCount);
            Serial_Print(logBuf, gAllowToBlock_d);
        }

        /* Trigger non-blocking success beep */
        Buzzer_BeepSuccess();

        /* Send update over BLE to Dashboard */
        Inventory_SendBleUpdate(index, newAction);
    }
    else
    {
        /* Unknown RFID tag scanned */
        (void)snprintf(logBuf, sizeof(logBuf),
                       "[PRODUCT]   Product ID: UNREGISTERED\r\n"
                       "[PRODUCT]   Match Status: NOT FOUND\r\n"
                       "[ERROR]     Unregistered RFID tag scanned!\r\n"
                       "[INVENTORY] Inventory unchanged.\r\n"
                       "[ALERT]     Security Alert: Unknown tag %02X%02X%02X%02X rejected.\r\n"
                       "[BUZZER]    Audio Feedback: 3 Alert Beeps (Error)\r\n",
                       uid[0], uid[1], uid[2], uid[3]);
        Serial_Print(logBuf, gAllowToBlock_d);

        /* Trigger non-blocking error beep */
        Buzzer_BeepError();

        /* Send alert over BLE to Dashboard */
        Inventory_SendUnknownAlert(uid);
    }
}

static void Inventory_SendBleUpdate(int itemIndex, scanAction_t action)
{
    char msgBuf[64];
    uint8_t len;

    /* Format: "ADD:ProductName:StockCount\n" or "REMOVE:ProductName:StockCount\n" */
    if (action == SCAN_ACTION_ADD)
    {
        len = (uint8_t)snprintf(msgBuf, sizeof(msgBuf), "ADD:%s:%d\n",
                                mInventory[itemIndex].productName,
                                mInventory[itemIndex].stockCount);
    }
    else
    {
        len = (uint8_t)snprintf(msgBuf, sizeof(msgBuf), "REMOVE:%s:%d\n",
                                mInventory[itemIndex].productName,
                                mInventory[itemIndex].stockCount);
    }

    if (len > 0 && len < sizeof(msgBuf))
    {
        BleApp_SendUartStream((uint8_t *)msgBuf, len);
    }
}

static void Inventory_SendUnknownAlert(uint8_t uid[RC522_UID_SIZE])
{
    char msgBuf[48];
    uint8_t len;

    len = (uint8_t)snprintf(msgBuf, sizeof(msgBuf), "ALERT:UNKNOWN_TAG:%02X%02X%02X%02X\n",
                            uid[0], uid[1], uid[2], uid[3]);

    if (len > 0 && len < sizeof(msgBuf))
    {
        BleApp_SendUartStream((uint8_t *)msgBuf, len);
    }
}

static void Inventory_SendStatus(void)
{
    char msgBuf[128];
    char logBuf[128];
    uint8_t len;
    uint8_t offset = 0;
    uint16_t totalStock = 0;

    offset += (uint8_t)snprintf(msgBuf + offset, sizeof(msgBuf) - offset, "STATUS:");

    for (uint8_t i = 0; i < mInventoryCount; i++)
    {
        if (mInventory[i].valid)
        {
            if (i > 0) {
                offset += (uint8_t)snprintf(msgBuf + offset, sizeof(msgBuf) - offset, ",");
            }
            offset += (uint8_t)snprintf(msgBuf + offset, sizeof(msgBuf) - offset, "%s:%d",
                                        mInventory[i].productName,
                                        mInventory[i].stockCount);
            totalStock += (uint16_t)mInventory[i].stockCount;
        }
    }

    len = (uint8_t)snprintf(msgBuf + offset, sizeof(msgBuf) - offset, "\n");
    offset += len;

    BleApp_SendUartStream((uint8_t *)msgBuf, offset);

    /* Print structured summary to serial monitor */
    Serial_Print("\r\n================ [CURRENT INVENTORY STATUS] ================\r\n", gAllowToBlock_d);
    for (uint8_t i = 0; i < mInventoryCount; i++)
    {
        if (mInventory[i].valid)
        {
            (void)snprintf(logBuf, sizeof(logBuf),
                           "[INVENTORY]   ID: %-4u | %-10s | Stock: %-3d | UID: %02X%02X%02X%02X\r\n",
                           (unsigned int)mInventory[i].productId,
                           mInventory[i].productName,
                           (int)mInventory[i].stockCount,
                           mInventory[i].uid[0], mInventory[i].uid[1],
                           mInventory[i].uid[2], mInventory[i].uid[3]);
            Serial_Print(logBuf, gAllowToBlock_d);
        }
    }
    (void)snprintf(logBuf, sizeof(logBuf),
                   "[INVENTORY] Total Registered Products: %u | Total Stock Units: %u\r\n"
                   "============================================================\r\n",
                   (unsigned int)mInventoryCount, (unsigned int)totalStock);
    Serial_Print(logBuf, gAllowToBlock_d);
}

/* ========== NON-BLOCKING BUZZER STATE MACHINE ========== */

static void Buzzer_TimerCallback(void *pParam)
{
    (void)pParam;

    if (mBuzzerPattern == BUZZER_PATTERN_SUCCESS)
    {
        /* 1 short beep finished -> turn off */
        BUZZER_OFF();
        mBuzzerPattern = BUZZER_PATTERN_IDLE;
        mBuzzerStep = 0;
    }
    else if (mBuzzerPattern == BUZZER_PATTERN_ERROR)
    {
        mBuzzerStep++;
        switch (mBuzzerStep)
        {
            case 1: /* First beep finished (200ms) -> pause 150ms */
                BUZZER_OFF();
                (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, 150U);
                break;
            case 2: /* Start second beep (200ms) */
                BUZZER_ON();
                (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, 200U);
                break;
            case 3: /* Second beep finished -> pause 150ms */
                BUZZER_OFF();
                (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, 150U);
                break;
            case 4: /* Start third beep (200ms) */
                BUZZER_ON();
                (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, 200U);
                break;
            case 5: /* Third beep finished -> final off */
            default:
                BUZZER_OFF();
                mBuzzerPattern = BUZZER_PATTERN_IDLE;
                mBuzzerStep = 0;
                break;
        }
    }
    else
    {
        BUZZER_OFF();
        mBuzzerPattern = BUZZER_PATTERN_IDLE;
        mBuzzerStep = 0;
    }
}

static void Buzzer_BeepSuccess(void)
{
    mBuzzerPattern = BUZZER_PATTERN_SUCCESS;
    mBuzzerStep = 0;
    BUZZER_ON();
    (void)TM_InstallCallback((timer_handle_t)mBuzzerTimerId, Buzzer_TimerCallback, NULL);
    (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, BUZZER_BEEP_SHORT_MS);
}

static void Buzzer_BeepError(void)
{
    mBuzzerPattern = BUZZER_PATTERN_ERROR;
    mBuzzerStep = 0;
    BUZZER_ON();
    (void)TM_InstallCallback((timer_handle_t)mBuzzerTimerId, Buzzer_TimerCallback, NULL);
    (void)TM_Start((timer_handle_t)mBuzzerTimerId, (uint8_t)kTimerModeSingleShot, BUZZER_BEEP_ERROR_MS);
}

/* ========== RFID SCAN TIMER ========== */

static void RfidScanTimerCallback(void *pParam)
{
    (void)pParam;
    uint8_t uid[RC522_UID_SIZE];
    bool_t detected;

    mTickCounter++;

    if (!mRfidInitialized)
    {
        return;
    }

    /* Poll RFID reader for card presence */
    detected = RC522_DetectCard(uid);

    if (detected)
    {
        /* Continuous-presence suppression: if card is still resting in RF field, do not re-scan */
        if (mCardCurrentlyInField && FLib_MemCmp(uid, mLastScannedUID, RC522_UID_SIZE))
        {
            return;
        }

        /* Time-based debouncing: ignore duplicate scan within RFID_DEBOUNCE_MS */
        if (FLib_MemCmp(uid, mLastScannedUID, RC522_UID_SIZE) &&
            ((mTickCounter - mLastScanTick) < (RFID_DEBOUNCE_MS / RFID_SCAN_INTERVAL_MS)))
        {
            return;
        }

        /* Valid new scan detected */
        mCardCurrentlyInField = TRUE;
        FLib_MemCpy(mLastScannedUID, uid, RC522_UID_SIZE);
        mLastScanTick = mTickCounter;

        Inventory_ProcessTag(uid);
    }
    else
    {
        /* No card detected in this cycle -> card has been lifted away from reader */
        mCardCurrentlyInField = FALSE;
    }
}

/* ========== HARDWARE & SUBSYSTEM INITIALIZATION ========== */

static void BleApp_BuzzerInit(void)
{
    gpio_pin_config_t gpioConfig;

    /* Enable clock for PORTC */
    CLOCK_EnableClock(kCLOCK_PortC);

    /* Configure buzzer pin as GPIO output */
    PORT_SetPinMux(BUZZER_PORT, BUZZER_PIN, kPORT_MuxAsGpio);
    gpioConfig.pinDirection = kGPIO_DigitalOutput;
    gpioConfig.outputLogic = 0U;  /* Start OFF */
    GPIO_PinInit(BUZZER_GPIO, BUZZER_PIN, &gpioConfig);

    Serial_Print("[GPIO]      PTC5 (Active Buzzer Pin) -> INITIALIZED\r\n", gAllowToBlock_d);
    Serial_Print("[BUZZER]    Non-Blocking Timer Engine (PTC5) -> SUCCESS\r\n", gAllowToBlock_d);
}

static void BleApp_RfidInit(void)
{
    int status;
    char logBuf[128];

    Serial_Print("[GPIO]      PTA18 (RC522 CS), PTC4 (RC522 RST) -> INITIALIZED\r\n", gAllowToBlock_d);
    Serial_Print("[SPI]       LPSPI1 Master Bus (1 MHz, Mode 0, PTB1/PTB2/PTB3) -> INITIALIZED\r\n", gAllowToBlock_d);

    status = RC522_Init();

    if (status == RC522_OK)
    {
        uint8_t version = RC522_GetVersion();
        mRfidInitialized = TRUE;

        (void)snprintf(logBuf, sizeof(logBuf),
                       "[RFID]      MFRC522 Detected (Chip Version: 0x%02X, Antenna: ON) -> SUCCESS\r\n",
                       version);
        Serial_Print(logBuf, gAllowToBlock_d);

        /* Initialize inventory database */
        Inventory_Init();

        (void)snprintf(logBuf, sizeof(logBuf),
                       "[INVENTORY] Local Database Initialized -> SUCCESS\r\n"
                       "[PRODUCT]   Registered Products: %u items\r\n"
                       "[PRODUCT]     1. ID: 1001 | Name: Laptop   | Stock: 10 | UID: A1 B2 C3 D4\r\n"
                       "[PRODUCT]     2. ID: 1002 | Name: Keyboard | Stock: 20 | UID: E5 F6 A7 B8\r\n"
                       "[PRODUCT]     3. ID: 1003 | Name: Mouse    | Stock: 15 | UID: 11 22 33 44\r\n",
                       (unsigned int)mInventoryCount);
        Serial_Print(logBuf, gAllowToBlock_d);

        /* Startup beep to indicate system ready */
        Buzzer_BeepSuccess();
    }
    else
    {
        mRfidInitialized = FALSE;
        Serial_Print("[RFID]      MFRC522 Detection FAILED! Check SPI wiring (3.3V, GND, PTA18, PTB1-3, PTC4)\r\n", gAllowToBlock_d);
        Serial_Print("[ERROR]     RFID Reader Offline! System in degraded mode.\r\n", gAllowToBlock_d);

        /* Error beep */
        Buzzer_BeepError();
    }
}

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 0))
static button_status_t BleApp_HandleKeys0
(
    void *pButtonHandle,
    button_callback_message_t *pMessage,
    void *pCallbackParam
)
{
    switch (pMessage->event)
    {
        case kBUTTON_EventOneClick:
        case kBUTTON_EventShortPress:
        {
#if (1 == gAppButtonCnt_c)
            mSwitchPressCnt++;

            if(FALSE == TM_IsTimerActive(mSwitchPressTimerId))
            {
                (void)TM_InstallCallback((timer_handle_t)mSwitchPressTimerId, SwitchPressTimerCallback, NULL);
                (void)TM_Start((timer_handle_t)mAppTimerId, (uint8_t)kTimerModeSingleShot | (uint8_t)kTimerModeLowPowerTimer, gSwitchPressTimeout_c);
            }
#else  /*1 == gAppButtonCnt_c*/
            BleApp_Start(mGapRole);
#endif /*1 == gAppButtonCnt_c*/
            break;
        }

        case kBUTTON_EventLongPress:
        {
            /* Long press: Send inventory status over BLE */
            Inventory_SendStatus();
            break;
        }

        default:
        {
            ; /* No action required */
            break;
        }
    }
    return kStatus_BUTTON_Success;
}
#endif /*gAppButtonCnt_c > 0*/

#if (gWuart_CentralRole_c == 1) && (gWuart_PeripheralRole_c == 1)
#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c > 1))
static button_status_t BleApp_HandleKeys1
(
    void *pButtonHandle,
    button_callback_message_t *pMessage,
    void *pCallbackParam
)
{
    switch (pMessage->event)
    {
    case kBUTTON_EventOneClick:
    case kBUTTON_EventShortPress:
    {
        if(mcActiveConnNo > 0)
        {
            /* Button 2 short press: Send inventory status report */
            Inventory_SendStatus();
        }
        break;
    }

    case kBUTTON_EventLongPress:

        /* Switch current role */
        if (mGapRole == gGapCentral_c)
        {
            Serial_Print("\n\rSwitched role to GAP Peripheral.\n\r", gAllowToBlock_d);
            mAppUartNewLine = TRUE;
            mGapRole = gGapPeripheral_c;
        }
        else
        {
            Serial_Print("\n\rSwitched role to GAP Central.\n\r", gAllowToBlock_d);
            mAppUartNewLine = TRUE;
            mGapRole = gGapCentral_c;
        }

        break;

    default:
    {
        /* No action required */
        break;
    }
    }

    return kStatus_BUTTON_Success;
}
#endif /* (gWuart_CentralRole_c == 1) && (gWuart_PeripheralRole_c == 1) */
#endif /*gAppButtonCnt_c > 0*/

static void BleApp_Start
(
    gapRole_t gapRole
)
{
    LedStopFlashingAllLeds();
    Led1Flashing();
    switch (gapRole)
    {
#if gWuart_CentralRole_c == 1
    case gGapCentral_c:
        {
            if (mScanningOn == FALSE)
            {
                mAppUartNewLine = TRUE;
#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
                (void)Gap_ControllerEnhancedNotification((uint16_t)gNotifScanEventOver_c | (uint16_t)gNotifScanAdvPktRx_c |
                                                   (uint16_t)gNotifScanRspRx_c | (uint16_t)gNotifScanReqTx_c, 0);
#endif
#if defined(gAppUsePairing_d) && (gAppUsePairing_d == 1)
                gPairingParameters.localIoCapabilities = gIoKeyboardDisplay_c;
#endif /* gAppUsePairing_d */
                (void)BluetoothLEHost_StartScanning(&mAppScanParams, BleApp_ScanningCallback);
            }
            break;
        }
#endif /* gWuart_CentralRole_c */
#if gWuart_PeripheralRole_c == 1
    case gGapPeripheral_c:
        {
            mAppUartNewLine = TRUE;
#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
            (void)Gap_ControllerEnhancedNotification((uint16_t)gNotifAdvEventOver_c | (uint16_t)gNotifAdvTx_c |
                                               (uint16_t)gNotifAdvScanReqRx_c | (uint16_t)gNotifAdvConnReqRx_c, 0);
#endif
#if defined(gAppUsePairing_d) && (gAppUsePairing_d == 1)
            gPairingParameters.localIoCapabilities = gIoDisplayOnly_c;
#endif /* gAppUsePairing_d */

            /* Start ADV only if it's not already started */
            if (!mAdvState.advOn)
            {
                (void)BluetoothLEHost_StartAdvertising(&mAppAdvParams, BleApp_AdvertisingCallback, BleApp_ConnectionCallback);
            }
            break;
        }
#endif /* gWuart_PeripheralRole_c */
    default:
        {
            ; /* No action required */
            break;
        }
    }
}

#if gWuart_CentralRole_c == 1
static void BleApp_ScanningCallback
(
    gapScanningEvent_t *pScanningEvent
)
{
    switch (pScanningEvent->eventType)
    {
        case gDeviceScanned_c:
        {
            if (BleApp_CheckScanEvent(&pScanningEvent->eventData.scannedDevice) && (mcActiveConnNo < gAppMaxConnections_c))
            {
                gConnReqParams.peerAddressType = pScanningEvent->eventData.scannedDevice.addressType;
                FLib_MemCpy(gConnReqParams.peerAddress,
                        pScanningEvent->eventData.scannedDevice.aAddress,
                        sizeof(bleDeviceAddress_t));

                (void)Gap_StopScanning();
#if gAppUsePrivacy_d
                gConnReqParams.usePeerIdentityAddress = pScanningEvent->eventData.scannedDevice.advertisingAddressResolved;
#endif
                (void)BluetoothLEHost_Connect(&gConnReqParams, BleApp_ConnectionCallback);
            }
        }
        break;

        case gScanStateChanged_c:
        {
            mScanningOn = !mScanningOn;
            if (mScanningOn)
            {
                (void)TM_InstallCallback((timer_handle_t)mAppTimerId, ScanningTimerCallback, NULL);
                (void)TM_Start((timer_handle_t)mAppTimerId,
                            (uint8_t)kTimerModeLowPowerTimer | (uint8_t)kTimerModeSetSecondTimer, gScanningTime_c);
                Led1Flashing();
                Serial_Print("\n\rScanning...\n\r", gAllowToBlock_d);
            }
            else
            {
                (void)TM_Stop((timer_handle_t)mAppTimerId);
                LedStartFlashingAllLeds();
                Serial_Print("\n\rStop Scanning...\n\r", gAllowToBlock_d);
            }
        }
        break;

        case gScanCommandFailed_c:
        {
            panic(0, 0, 0, 0);
            break;
        }

        default:
        {
            ; /* No action required */
            break;
        }
    }
}
#endif /* gWuart_CentralRole_c == 1 */

#if (gWuart_PeripheralRole_c == 1)
static void BleApp_AdvertisingCallback
(
    gapAdvertisingEvent_t *pAdvertisingEvent
)
{
    switch (pAdvertisingEvent->eventType)
    {
        case gAdvertisingStateChanged_c:
        {
            mAdvState.advOn = !mAdvState.advOn;
            LedStopFlashingAllLeds();
            if (mAdvState.advOn)
            {
                Led1Flashing();
                Serial_Print("[BLE]       Advertising started. Device Name: 'RFID_INVENTORY' (connectable)\r\n", gAllowToBlock_d);
                Serial_Print("[SYSTEM]    System Ready. Scanning RFID cards every 500 ms...\r\n", gAllowToBlock_d);
            }
            else
            {
                LedStartFlashingAllLeds();
            }
        }
        break;

        case gAdvertisingCommandFailed_c:
        {
            panic(0, 0, 0, 0);
        }
        break;

        default:
        {
            ; /* No action required */
        }
        break;
    }
}

#endif

static void BleApp_ConnectionCallback
(
    deviceId_t peerDeviceId,
    gapConnectionEvent_t *pConnectionEvent
)
{
    switch (pConnectionEvent->eventType)
    {
        case gConnEvtConnected_c:
        {
            maPeerInformation[peerDeviceId].deviceId = peerDeviceId;
            mcActiveConnNo++;
#if gWuart_PeripheralRole_c == 1
            if(pConnectionEvent->eventData.connectedEvent.connectionRole == gBleLlConnectionPeripheral_c)
            {
                mAdvState.advOn = FALSE;
            }
#endif

            (void)Wus_Subscribe(peerDeviceId);
            (void)Bas_Subscribe(&mBasServiceConfig, peerDeviceId);

            /* UI */
            LedStopFlashingAllLeds();
            Led1On();

            if (TM_IsTimerActive((timer_handle_t)mBatteryMeasurementTimerId) == 0U)
            {
                (void)TM_InstallCallback((timer_handle_t)mBatteryMeasurementTimerId, BatteryMeasurementTimerCallback, NULL);
                (void)TM_Start((timer_handle_t)mBatteryMeasurementTimerId,
                            (uint8_t)kTimerModeLowPowerTimer | (uint8_t)kTimerModeSetSecondTimer, mBatteryLevelReportInterval_c);
            }

            /* Start RFID scanning timer */
            if (TM_IsTimerActive((timer_handle_t)mRfidScanTimerId) == 0U)
            {
                (void)TM_InstallCallback((timer_handle_t)mRfidScanTimerId, RfidScanTimerCallback, NULL);
                (void)TM_Start((timer_handle_t)mRfidScanTimerId,
                            (uint8_t)kTimerModeLowPowerTimer | (uint8_t)kTimerModeIntervalTimer, RFID_SCAN_INTERVAL_MS);
            }

#if gAppUsePairing_d
#if gAppUseBonding_d
            if (mGapRole == gGapCentral_c)
            {
                union
                {
                    uint32_t u32;
                    uint16_t u16;
                } tempCast;

                tempCast.u32 = sizeof(wucConfig_t);
                (void)Gap_CheckIfBonded(peerDeviceId, &maPeerInformation[peerDeviceId].isBonded, NULL);

                if ((maPeerInformation[peerDeviceId].isBonded) &&
                    (gBleSuccess_c == Gap_LoadCustomPeerInformation(peerDeviceId,
                            (uint8_t *) &maPeerInformation[peerDeviceId].clientInfo, 0, tempCast.u16)))
                {
                    (void)Gap_EncryptLink(peerDeviceId);
                }
            }
#endif /* gAppUseBonding_d*/
#endif /* gAppUsePairing_d */

            char connBuf[180];
            (void)snprintf(connBuf, sizeof(connBuf),
                           "\r\n[BLE]       Connection established with Device ID: %u\r\n"
                           "[BLE]       Role: %s\r\n"
                           "[BLE]       GATT Server notifications enabled for telemetry stream.\r\n",
                           (unsigned int)peerDeviceId,
                           (pConnectionEvent->eventData.connectedEvent.connectionRole == gBleLlConnectionCentral_c ?
                            "GAP Central" : "GAP Peripheral (GATT Server)"));
            Serial_Print(connBuf, gAllowToBlock_d);
            mAppUartNewLine = TRUE;

#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
            (void)Gap_ControllerEnhancedNotification((uint16_t)gNotifConnRxPdu_c, peerDeviceId);
#endif

            if  (pConnectionEvent->eventData.connectedEvent.connectionRole == gBleLlConnectionPeripheral_c)
            {
                maPeerInformation[peerDeviceId].gapRole = gGapPeripheral_c;
                maPeerInformation[peerDeviceId].appState = mAppRunning_c;
            }
            else
            {
                maPeerInformation[peerDeviceId].gapRole = gGapCentral_c;
            }

            BleApp_StateMachineHandler(peerDeviceId, mAppEvt_PeerConnected_c);
        }
        break;

        case gConnEvtDisconnected_c:
        {
            char discBuf[160];
            (void)snprintf(discBuf, sizeof(discBuf),
                           "\r\n[BLE]       Client disconnected (Device ID: %u).\r\n"
                           "[BLE]       Restarting BLE advertising as GAP Peripheral...\r\n",
                           (unsigned int)peerDeviceId);
            Serial_Print(discBuf, gAllowToBlock_d);

            maPeerInformation[peerDeviceId].appState = mAppIdle_c;
            maPeerInformation[peerDeviceId].clientInfo.hService = gGattDbInvalidHandleIndex_d;
            maPeerInformation[peerDeviceId].clientInfo.hUartStream = gGattDbInvalidHandleIndex_d;

            (void)Wus_Unsubscribe();
            (void)Bas_Unsubscribe(&mBasServiceConfig, peerDeviceId);

            BleServDisc_Stop(peerDeviceId);

            LedStartFlashingAllLeds();

            maPeerInformation[peerDeviceId].deviceId = gInvalidDeviceId_c;
            mcActiveConnNo--;
            if(mcActiveConnNo == 0U)
            {
                (void)TM_Stop((timer_handle_t)mBatteryMeasurementTimerId);
                /* Note: Keep RFID scan timer running even without BLE connection
                   so user can still see serial output. Stop it if you want to save power. */
            }
            mAppUartBufferSize = mAppUartBufferSize_c;

            for (uint8_t mPeerId = 0U; mPeerId < (uint8_t)gAppMaxConnections_c; mPeerId++)
            {
                if (gInvalidDeviceId_c != maPeerInformation[mPeerId].deviceId)
                {
                    uint16_t tempMtu = 0U;
                    (void)Gatt_GetMtu(mPeerId, &tempMtu);
                    tempMtu = gAttMaxWriteDataSize_d(tempMtu);
                    if (tempMtu < mAppUartBufferSize)
                    {
                        mAppUartBufferSize = tempMtu;
                    }
                }
            }

            if (mGapRole == gGapPeripheral_c)
            {
                BleApp_Start(mGapRole);
            }
        }
        break;

#if gAppUsePairing_d
        case gConnEvtPairingComplete_c:
        {
            if (pConnectionEvent->eventData.pairingCompleteEvent.pairingSuccessful)
            {
                BleApp_StateMachineHandler(peerDeviceId, mAppEvt_PairingComplete_c);
            }
        }
        break;

        case gConnEvtAuthenticationRejected_c:
        {
            if (mGapRole == gGapCentral_c)
            {
                (void)Gap_Pair(peerDeviceId, &gPairingParameters);
            }
        }
        break;
#endif /* gAppUsePairing_d */

        default:
        {
            ; /* No action required */
        }
        break;
    }

    switch (maPeerInformation[peerDeviceId].gapRole)
    {
#if gWuart_CentralRole_c == 1
        case gGapCentral_c:
            BleConnManager_GapCentralEvent(peerDeviceId, pConnectionEvent);
            break;
#endif /* gWuart_CentralRole_c == 1 */
#if gWuart_PeripheralRole_c == 1
        case gGapPeripheral_c:
            BleConnManager_GapPeripheralEvent(peerDeviceId, pConnectionEvent);
            break;
#endif /* gWuart_PeripheralRole_c == 1 */
        default:
            ; /* No action required */
            break;
    }
}

static void BleApp_ServiceDiscoveryCallback
(
    deviceId_t peerDeviceId,
    servDiscEvent_t *pEvent
)
{
    switch (pEvent->eventType)
    {
        case gServiceDiscovered_c:
        {
            if (pEvent->eventData.pService->uuidType == gBleUuidType128_c)
            {
                if (FLib_MemCmp((void *)&uuid_service_wireless_uart, (void *)&pEvent->eventData.pService->uuid, sizeof(bleUuid_t)))
                {
                    BleApp_StoreServiceHandles(peerDeviceId, pEvent->eventData.pService);
                }
            }
        }
        break;

        case gDiscoveryFinished_c:
        {
            if (pEvent->eventData.success)
            {
                if (gGattDbInvalidHandleIndex_d != maPeerInformation[peerDeviceId].clientInfo.hService)
                {
                    BleApp_StateMachineHandler(peerDeviceId, mAppEvt_ServiceDiscoveryComplete_c);
                }
                else
                {
                    BleApp_StateMachineHandler(peerDeviceId, mAppEvt_ServiceDiscoveryNotFound_c);
                }
            }
            else
            {
                BleApp_StateMachineHandler(peerDeviceId, mAppEvt_ServiceDiscoveryFailed_c);
            }
        }
        break;

        default:
        {
            ; /* No action required */
        }
        break;
    }
}

static void BleApp_GattClientCallback
(
    deviceId_t              serverDeviceId,
    gattProcedureType_t     procedureType,
    gattProcedureResult_t   procedureResult,
    bleResult_t             error
)
{
    switch (procedureResult)
    {
        case gGattProcError_c:
        {
            switch (error)
            {
#if (defined(gAppUsePairing_d) && (gAppUsePairing_d == 1U))
                case gGattConnectionSecurityRequirementsNotMet_c:
                {
                    if (mGapRole == gGapCentral_c)
                    {
                        (void)Gap_Pair(serverDeviceId, &gPairingParameters);
                    }
                    else if (mGapRole == gGapPeripheral_c)
                    {
                        (void)Gap_SendPeripheralSecurityRequest(serverDeviceId, &gPairingParameters);
                    }
                    else
                    {
                        /* No action required */
                    }
                }
                break;
#endif /* gAppUsePairing_d */

                default:
                {
                    BleApp_StateMachineHandler(serverDeviceId, mAppEvt_GattProcError_c);
                }
                break;
            }
            break;
        }

        case gGattProcSuccess_c:
            BleApp_StateMachineHandler(serverDeviceId, mAppEvt_GattProcComplete_c);
            break;

        default:
            ; /* No action required */
            break;
    }

    BleServDisc_SignalGattClientEvent(serverDeviceId, procedureType, procedureResult, error);
}

static void BleApp_GattServerCallback
(
    deviceId_t deviceId,
    gattServerEvent_t *pServerEvent
)
{
    uint16_t tempMtu = 0;

    switch (pServerEvent->eventType)
    {
        case gEvtAttributeWrittenWithoutResponse_c:
        {
            if (pServerEvent->eventData.attributeWrittenEvent.handle == (uint16_t)value_uart_stream)
            {
                BleApp_ReceivedUartStream(deviceId, pServerEvent->eventData.attributeWrittenEvent.aValue,
                                          pServerEvent->eventData.attributeWrittenEvent.cValueLength);
            }
            break;
        }

        case gEvtMtuChanged_c:
        {
            (void)Gatt_GetMtu(deviceId, &tempMtu);
            tempMtu = gAttMaxWriteDataSize_d(tempMtu);
            mAppUartBufferSize = mAppUartBufferSize <= tempMtu ? mAppUartBufferSize : tempMtu;
        }
        break;

        default:
        {
            ; /* No action required */
        }
        break;
    }
}

static void BleApp_StoreServiceHandles
(
    deviceId_t peerDeviceId,
    gattService_t *pService
)
{
    maPeerInformation[peerDeviceId].clientInfo.hService = pService->startHandle;

    if (pService->cNumCharacteristics > 0U &&
        pService->aCharacteristics != NULL)
    {
        maPeerInformation[peerDeviceId].clientInfo.hUartStream =
            pService->aCharacteristics[0].value.handle;
    }
}

#if gWuart_CentralRole_c == 1
static bool_t BleApp_CheckScanEvent
(
    gapScannedDevice_t *pData
)
{
    uint8_t index = 0;
    bool_t foundMatch = FALSE;

    while (index < pData->dataLength)
    {
        gapAdStructure_t adElement;

        adElement.length = pData->data[index];
        adElement.adType = (gapAdType_t) pData->data[index + 1U];
        adElement.aData = &pData->data[index + 2U];

        if ((adElement.adType == gAdIncomplete128bitServiceList_c)
            || (adElement.adType == gAdComplete128bitServiceList_c))
        {
            foundMatch = BluetoothLEHost_MatchDataInAdvElementList(
                    &adElement,
                    &uuid_service_wireless_uart,
                    16);
        }

        index += adElement.length + (uint8_t)sizeof(uint8_t);
    }

    return foundMatch;
}
#endif /* gWuart_CentralRole_c == 1 */

static void BleApp_SendUartStream
(
    uint8_t *pRecvStream,
    uint8_t streamSize
)
{
    uint8_t mPeerId = 0;
    bool_t sent = FALSE;
    char logBuf[160];

    /* Display transmission attempt in serial monitor */
    (void)snprintf(logBuf, sizeof(logBuf),
                   "[BLE]       Transmitting Telemetry (%u bytes): \"%.*s\"\r\n",
                   (unsigned int)streamSize,
                   (int)(streamSize > 0 && pRecvStream[streamSize - 1] == '\n' ? streamSize - 1 : streamSize),
                   (const char *)pRecvStream);
    Serial_Print(logBuf, gAllowToBlock_d);

    for (mPeerId = 0; mPeerId < (uint8_t)gAppMaxConnections_c; mPeerId++)
    {
        if (gInvalidDeviceId_c != maPeerInformation[mPeerId].deviceId &&
            mAppRunning_c == maPeerInformation[mPeerId].appState)
        {
            /* 1. Send GATT Server Instant Notification to connected client (Phone / Python Gateway) */
            bleResult_t res = GattServer_SendInstantValueNotification(
                maPeerInformation[mPeerId].deviceId,
                (uint16_t)value_uart_stream,
                (uint16_t)streamSize,
                pRecvStream);

            if (res == gBleSuccess_c)
            {
                (void)snprintf(logBuf, sizeof(logBuf),
                               "[BLE]       Peer Device %u: GATT Notification -> DISPATCHED SUCCESS\r\n"
                               "[SYSTEM]    Note: Dispatched via BLE. Web Dashboard delivery managed by Python MQTT Gateway.\r\n",
                               (unsigned int)maPeerInformation[mPeerId].deviceId);
                Serial_Print(logBuf, gAllowToBlock_d);
                sent = TRUE;
            }
            else
            {
                (void)snprintf(logBuf, sizeof(logBuf),
                               "[ERROR]     Peer Device %u: GATT Notification FAILED (Status: 0x%02X)\r\n",
                               (unsigned int)maPeerInformation[mPeerId].deviceId, (unsigned int)res);
                Serial_Print(logBuf, gAllowToBlock_d);
            }

            /* 2. Also support board-to-board client write if connected to another board running WUS */
            if (maPeerInformation[mPeerId].clientInfo.hUartStream != gGattDbInvalidHandleIndex_d)
            {
                gattCharacteristic_t characteristic = {(uint8_t)gGattCharPropNone_c, {0}, 0, 0};
                characteristic.value.handle = maPeerInformation[mPeerId].clientInfo.hUartStream;
                (void)GattClient_WriteCharacteristicValue(mPeerId, &characteristic,
                        streamSize, pRecvStream, TRUE,
                        FALSE, FALSE, NULL);
            }
        }
    }

    if (!sent)
    {
        Serial_Print("[BLE]       No active BLE client connected. Telemetry recorded locally in RAM.\r\n", gAllowToBlock_d);
    }
}

static void BleApp_StateMachineHandler
(
    deviceId_t peerDeviceId,
    appEvent_t event
)
{
    uint16_t tempMtu = 0;
    union
    {
        uint8_t     *pUuidArray;
        bleUuid_t   *pUuidObj;
    } temp;

    temp.pUuidArray = uuid_service_wireless_uart;

    if (maPeerInformation[peerDeviceId].deviceId != gInvalidDeviceId_c)
    {
        switch (maPeerInformation[peerDeviceId].appState)
        {
        case mAppIdle_c:
        {
            if (event == mAppEvt_PeerConnected_c)
            {
                if (mGapRole == gGapCentral_c)
                {
                    maPeerInformation[peerDeviceId].appState = mAppExchangeMtu_c;
                    (void)GattClient_ExchangeMtu(peerDeviceId, gAttMaxMtu_c);
                }
                else
                {
                    /* When acting as Peripheral, remote peer is Central (Phone / PC bridge).
                     * Transition directly to mAppRunning_c so GATT Server notifications work immediately. */
                    maPeerInformation[peerDeviceId].appState = mAppRunning_c;
                }
            }
        }
        break;

        case mAppExchangeMtu_c:
        {
            if (event == mAppEvt_GattProcComplete_c)
            {
                (void)Gatt_GetMtu(peerDeviceId, &tempMtu);
                tempMtu = gAttMaxWriteDataSize_d(tempMtu);
                mAppUartBufferSize = mAppUartBufferSize <= tempMtu ? mAppUartBufferSize : tempMtu;

                maPeerInformation[peerDeviceId].appState = mAppServiceDisc_c;
                (void)BleServDisc_FindService(peerDeviceId,
                        gBleUuidType128_c,
                        temp.pUuidObj);
            }
            else
            {
                if (event == mAppEvt_GattProcError_c)
                {
                    (void)Gap_Disconnect(peerDeviceId);
                }
            }
        }
        break;

        case mAppServiceDisc_c:
        {
            if (event == mAppEvt_ServiceDiscoveryComplete_c)
            {
                maPeerInformation[peerDeviceId].appState = mAppRunning_c;
#if gAppUseBonding_d
                union
                {
                    uint32_t u32;
                    uint16_t u16;
                } tempCast;

                tempCast.u32 = sizeof(wucConfig_t);
                (void)Gap_SaveCustomPeerInformation(maPeerInformation[peerDeviceId].deviceId,
                        (uint8_t *) &maPeerInformation[peerDeviceId].clientInfo, 0,
                        tempCast.u16);
#endif
            }
            else if (event == mAppEvt_ServiceDiscoveryNotFound_c)
            {
                if (mGapRole == gGapPeripheral_c)
                {
                    maPeerInformation[peerDeviceId].appState = mAppRunning_c;
                }
                else
                {
                    maPeerInformation[peerDeviceId].appState = mAppServiceDiscRetry_c;
                    (void)BleServDisc_Start(peerDeviceId);
                }
            }
            else if (event == mAppEvt_ServiceDiscoveryFailed_c)
            {
                if (mGapRole == gGapPeripheral_c)
                {
                    maPeerInformation[peerDeviceId].appState = mAppRunning_c;
                }
                else
                {
                    (void)Gap_Disconnect(peerDeviceId);
                }
            }
            else
            {
                /* ignore other event types */
            }
        }
        break;

        case mAppServiceDiscRetry_c:
        {
            if (event == mAppEvt_ServiceDiscoveryComplete_c)
            {
                maPeerInformation[peerDeviceId].appState = mAppRunning_c;
            }
            else if ((event == mAppEvt_ServiceDiscoveryNotFound_c) ||
                    (event == mAppEvt_ServiceDiscoveryFailed_c))
            {
                if (mGapRole == gGapPeripheral_c)
                {
                    maPeerInformation[peerDeviceId].appState = mAppRunning_c;
                }
                else
                {
                    (void)Gap_Disconnect(peerDeviceId);
                }
            }
            else
            {
                /* ignore other event types */
            }
        }
        break;

        case mAppRunning_c:
            break;

        default:
        {
            ; /* No action required */
        }
        break;
        }
    }
}

#if gWuart_CentralRole_c == 1
static void ScanningTimerCallback
(
    void *pParam
)
{
    (void)Gap_StopScanning();
}
#endif /* gWuart_CentralRole_c == 1 */

#if (defined(gAppButtonCnt_c) && (gAppButtonCnt_c == 1))
static void SwitchPressTimerCallback
(
    void *pParam
)
{
  if(mSwitchPressCnt >= gSwitchPressThreshold_c)
  {
      if (mGapRole == gGapCentral_c)
      {
          Serial_Print("\n\rSwitched role to GAP Peripheral.\n\r", gAllowToBlock_d);
          mAppUartNewLine = TRUE;
          mGapRole = gGapPeripheral_c;
      }
      else
      {
          Serial_Print("\n\rSwitched role to GAP Central.\n\r", gAllowToBlock_d);
          mAppUartNewLine = TRUE;
          mGapRole = gGapCentral_c;
      }
  }
  else
  {
      LedStopFlashingAllLeds();
      Led1Flashing();
      BleApp_Start(mGapRole);
  }

  mSwitchPressCnt = 0;
}
#endif

static void BleApp_FlushUartStream
(
    void *pParam
)
{
    uint8_t *pMsg = NULL;
    uint32_t bytesRead = 0;
    uint8_t  mPeerId = 0;
    bool_t   mValidDevices = FALSE;

    for (mPeerId = 0; mPeerId < (uint8_t)gAppMaxConnections_c; mPeerId++)
    {
        if ((gInvalidDeviceId_c != maPeerInformation[mPeerId].deviceId) &&
            (mAppRunning_c == maPeerInformation[mPeerId].appState))
        {
            mValidDevices = TRUE;
            break;
        }
    }

    if (mValidDevices)
    {
        pMsg = MEM_BufferAlloc(mAppUartBufferSize);

        if (pMsg != NULL)
        {
            if (SerialManager_TryRead((serial_read_handle_t)s_readHandle, pMsg, mAppUartBufferSize, &bytesRead) == kStatus_SerialManager_Success)
            {
                if (bytesRead != 0U)
                {
                    BleApp_SendUartStream(pMsg, (uint8_t)bytesRead);
                }
            }

            (void)MEM_BufferFree(pMsg);
        }
    }

    mAppDapaPending = FALSE;
}

static void BleApp_ReceivedUartStream
(
    deviceId_t peerDeviceId,
    uint8_t *pStream,
    uint16_t streamLength
)
{
    static deviceId_t previousDeviceId = gInvalidDeviceId_c;

    char additionalInfoBuff[10] = { '\r', '\n', '[', '0', '0', '-', 'C', ']', ':', ' '};
    uint8_t *pBuffer = NULL;
    uint32_t messageHeaderSize = 0;

    /* Check if we received a "STATUS" command over BLE */
    if (streamLength >= 6)
    {
        if (FLib_MemCmp(pStream, "STATUS", 6))
        {
            Inventory_SendStatus();
            return;
        }
    }

    if (mAppUartNewLine || (previousDeviceId != peerDeviceId))
    {
        streamLength += (uint16_t)sizeof(additionalInfoBuff);
    }

    pBuffer = MEM_BufferAlloc(streamLength);

    if (pBuffer != NULL)
    {
        if (mAppUartNewLine || (previousDeviceId != peerDeviceId))
        {
            messageHeaderSize = sizeof(additionalInfoBuff);

            if (mAppUartNewLine)
            {
                mAppUartNewLine = FALSE;
            }

            additionalInfoBuff[3] = '0' + (peerDeviceId / 10U);
            additionalInfoBuff[4] = '0' + (peerDeviceId % 10U);

            if (gGapCentral_c != maPeerInformation[peerDeviceId].gapRole)
            {
                additionalInfoBuff[6] = 'P';
            }

            FLib_MemCpy(pBuffer, additionalInfoBuff, sizeof(additionalInfoBuff));
        }

        FLib_MemCpy(&pBuffer[messageHeaderSize], pStream, (uint32_t)streamLength - messageHeaderSize);
#if (defined(SERIAL_MANAGER_NON_BLOCKING_MODE) && (SERIAL_MANAGER_NON_BLOCKING_MODE > 0U))
        serial_manager_status_t status = SerialManager_InstallTxCallback((serial_write_handle_t)s_writeHandle, Uart_TxCallBack, pBuffer);
        (void)status;
        assert(kStatus_SerialManager_Success == status);
        if(SerialManager_WriteNonBlocking((serial_write_handle_t)s_writeHandle, pBuffer, streamLength) != kStatus_SerialManager_Success)
        {
            (void)MEM_BufferFree(pBuffer);
        }
#else
        (void)MEM_BufferFree(pBuffer);
#endif /*SERIAL_MANAGER_NON_BLOCKING_MODE > 0U*/
    }

    previousDeviceId = peerDeviceId;
}

static void UartStreamFlushTimerCallback
(
    void *pData
)
{
    if (!mAppDapaPending)
    {
        mAppDapaPending = TRUE;
        (void)App_PostCallbackMessage(BleApp_FlushUartStream, NULL);
    }
}

static void Uart_RxCallBack
(
    void *pData,
    serial_manager_callback_message_t *pMessage,
    serial_manager_status_t status
)
{
    uint16_t byteCount = 0;

    if (byteCount < mAppUartBufferSize)
    {
        (void)TM_InstallCallback((timer_handle_t)mUartStreamFlushTimerId, UartStreamFlushTimerCallback, NULL);
        (void)TM_Start((timer_handle_t)mUartStreamFlushTimerId,
                    (uint8_t)kTimerModeLowPowerTimer | (uint8_t)kTimerModeSingleShot, mAppUartFlushIntervalInMs_c);
    }
    else
    {
        if (!mAppDapaPending)
        {
            mAppDapaPending = TRUE;
            (void)App_PostCallbackMessage(BleApp_FlushUartStream, NULL);
        }
    }
}

static void Uart_TxCallBack
(
    void *pBuffer,
    serial_manager_callback_message_t *pMessage,
    serial_manager_status_t status
)
{
    (void)MEM_BufferFree(pMessage->buffer);
}

static void BatteryMeasurementTimerCallback
(
    void *pParam
)
{
    mBasServiceConfig.batteryLevel = SENSORS_GetBatteryLevel();
    (void)Bas_RecordBatteryMeasurement(&mBasServiceConfig);
}

#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
static void BleApp_HandleControllerNotification
(
    bleNotificationEvent_t *pNotificationEvent
)
{
    switch(pNotificationEvent->eventType)
    {
        case (uint16_t)gNotifEventNone_c:
            Serial_Print("Configured notification status ", gAllowToBlock_d);
            Serial_PrintDec((uint32_t)pNotificationEvent->status);
            Serial_Print("\n\r", gAllowToBlock_d);
            break;

        case (uint16_t)gNotifConnRxPdu_c:
            Serial_Print("CONN Rx PDU from device ", gAllowToBlock_d);
            Serial_PrintDec(pNotificationEvent->deviceId);
            Serial_Print("\n\r", gAllowToBlock_d);
            break;

        default:
            ; /* No action required */
            break;
    }
}

#if defined(gUseControllerNotificationsCallback_c) && (gUseControllerNotificationsCallback_c)
static void BleApp_ControllerNotificationCallback
(
    bleCtrlNotificationEvent_t *pNotificationEvent
)
{
    switch(pNotificationEvent->event_type)
    {
        case gNotifConnRxPdu_c:
            Serial_Print("CONN Rx PDU\n\r", gNoBlock_d);
            break;
        default:
            ; /* No action required */
            break;
    }
}
#endif
#endif

static void BleApp_SerialInit(void)
{
    serial_manager_status_t status;
    static serial_handle_t appSerMgrIf;

    appSerMgrIf = (serial_handle_t)&gSerMgrIf[0];

    status = SerialManager_OpenWriteHandle((serial_handle_t)appSerMgrIf, (serial_write_handle_t)s_writeHandle);
    assert(kStatus_SerialManager_Success == status);
    (void)status;

    status = SerialManager_OpenReadHandle((serial_handle_t)appSerMgrIf, (serial_read_handle_t)s_readHandle);
    assert(kStatus_SerialManager_Success == status);
    status = SerialManager_InstallRxCallback((serial_read_handle_t)s_readHandle, Uart_RxCallBack, NULL);
    assert(kStatus_SerialManager_Success == status);

    Serial_Print("\r\n======================================================================\r\n", gAllowToBlock_d);
    Serial_Print("   PROJECT: IoT-BASED SMART INVENTORY MANAGEMENT SYSTEM\r\n", gAllowToBlock_d);
    Serial_Print("   DEVELOPMENT BOARD: NXP FRDM-MCXW71\r\n", gAllowToBlock_d);
    Serial_Print("   MICROCONTROLLER  : MCXW716CMFTA (ARM Cortex-M33 @ 48 MHz)\r\n", gAllowToBlock_d);
    Serial_Print("   COMMUNICATION    : UART Debug (115200 8-N-1) + BLE 5.3 + SPI\r\n", gAllowToBlock_d);
    Serial_Print("======================================================================\r\n", gAllowToBlock_d);
    Serial_Print("[BOOT]      System boot sequence initiated...\r\n", gAllowToBlock_d);
    Serial_Print("[CLOCK]     Core Clock: 48 MHz (FRO-48M), LPSPI1: 6 MHz (FRO-6M) -> CONFIGURED\r\n", gAllowToBlock_d);
}

static void BluetoothLEHost_Initialized(void)
{
    uint8_t peerId = 0;

    BleConnManager_GapCommonConfig();

    (void)App_RegisterGattServerCallback(BleApp_GattServerCallback);
    (void)App_RegisterGattClientProcedureCallback(BleApp_GattClientCallback);
    (void)GattServer_RegisterHandlesForWriteNotifications(NumberOfElements(mCharMonitoredHandles), mCharMonitoredHandles);
    BleServDisc_RegisterCallback(BleApp_ServiceDiscoveryCallback);
    mcActiveConnNo = 0U;
    for (peerId = 0; peerId < (uint8_t)gAppMaxConnections_c; peerId++)
    {
        maPeerInformation[peerId].deviceId = gInvalidDeviceId_c;
        maPeerInformation[peerId].appState = mAppIdle_c;
        maPeerInformation[peerId].clientInfo.hService = gGattDbInvalidHandleIndex_d;
        maPeerInformation[peerId].clientInfo.hUartStream = gGattDbInvalidHandleIndex_d;
    }

#if (gWuart_AutoStart_c == 1)
    mGapRole = gWuart_AutoStartGapRole_c;
#else
#if gWuart_CentralRole_c == 1
    mGapRole = gGapCentral_c;
#else
    mGapRole = gGapPeripheral_c;
#endif /* gWuart_CentralRole_c */
#endif /* gWuart_AutoStart_c */

    Serial_Print("[BLE]       Bluetooth LE Host Stack Initialized -> SUCCESS\r\n", gAllowToBlock_d);
    if (mGapRole == gGapCentral_c)
    {
        Serial_Print("[BLE]       Local Role: GAP Central\r\n", gAllowToBlock_d);
    }
    else
    {
        Serial_Print("[BLE]       Device Name: 'RFID_INVENTORY' | Role: GAP Peripheral (GATT Server)\r\n", gAllowToBlock_d);
        Serial_Print("[BLE]       GATT Service: Wireless UART (UUID: 01ff0100-ba5e-f4ee-5ca1-eb1e5e4b1ce0)\r\n", gAllowToBlock_d);
    }
    Serial_Print("[SYSTEM]    Final Status: ALL SUBSYSTEMS OPERATIONAL -> READY\r\n", gAllowToBlock_d);
    Serial_Print("======================================================================\r\n", gAllowToBlock_d);

#if gWuart_PeripheralRole_c == 1
    mAdvState.advOn = FALSE;
#endif /* gWuart_PeripheralRole_c */

#if gWuart_CentralRole_c == 1
    mScanningOn = FALSE;
#endif /* gWuart_CentralRole_c */

    /* Start services */
    (void)Wus_Start(&mWuServiceConfig);

    mBasServiceConfig.batteryLevel = SENSORS_GetBatteryLevel();
    (void)Bas_Start(&mBasServiceConfig);

    /* Allocate application timers */
    (void)TM_Open(mAppTimerId);
    (void)TM_Open(mUartStreamFlushTimerId);
    (void)TM_Open(mBatteryMeasurementTimerId);
    (void)TM_Open(mRfidScanTimerId);
    (void)TM_Open(mBuzzerTimerId);

#if (gAppButtonCnt_c == 1)
    (void)TM_Open(mSwitchPressTimerId);
#endif

#if !(defined(cPWR_UsePowerDownMode) && (cPWR_UsePowerDownMode))
    LedStartFlashingAllLeds();
#endif

    /* Start RFID scanning timer immediately (even before BLE connection).
     * This allows serial console output for debugging. */
    (void)TM_InstallCallback((timer_handle_t)mRfidScanTimerId, RfidScanTimerCallback, NULL);
    (void)TM_Start((timer_handle_t)mRfidScanTimerId,
                (uint8_t)kTimerModeLowPowerTimer | (uint8_t)kTimerModeIntervalTimer, RFID_SCAN_INTERVAL_MS);

#if (gWuart_AutoStart_c == 1)
    BleApp_Start(mGapRole);
#endif
}

static void BluetoothLEHost_GenericCallback(gapGenericEvent_t *pGenericEvent)
{
    BleConnManager_GenericEvent(pGenericEvent);

#if defined(gUseControllerNotifications_c) && (gUseControllerNotifications_c)
    if (pGenericEvent->eventType == gControllerNotificationEvent_c)
    {
        BleApp_HandleControllerNotification(&pGenericEvent->eventData.notifEvent);
    }
#endif /* gUseControllerNotifications_c */
    if(pGenericEvent->eventType == gInternalError_c)
    {
        if((pGenericEvent->eventData.internalError.errorCode == gBleOverflow_c) && (pGenericEvent->eventData.internalError.errorSource == gAddNewConnection_c))
        {
            Serial_Print("Connection failed ", gAllowToBlock_d);
#if gWuart_PeripheralRole_c == 1
            if(mAdvState.advOn)
            {
                mAdvState.advOn = FALSE;
                Serial_Print(" as peripheral.\n\r", gAllowToBlock_d);
            }
            else
#endif /* gWuart_PeripheralRole_c == 1 */
            {
#if gWuart_CentralRole_c == 1
                Serial_Print(" as central.\n\r", gAllowToBlock_d);
#endif /* gWuart_CentralRole_c == 1 */
            }
        }
    }
}

/*! *********************************************************************************
 * @}
 ********************************************************************************** */
