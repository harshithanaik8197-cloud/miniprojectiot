/*! *********************************************************************************
 * \file mfrc522.c
 *
 * MFRC522 RFID Reader Driver Implementation for NXP FRDM-MCXW71
 * Microcontroller: NXP MCXW716C (Arm Cortex-M33)
 * Peripheral     : LPSPI1
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ********************************************************************************** */

#include "mfrc522.h"
#include "fsl_clock.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_lpspi.h"
#include "fsl_component_serial_manager.h"
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 * Hardware Configuration Definitions
 ******************************************************************************/
#define MFRC522_SPI_BASE            LPSPI1
#define MFRC522_SPI_BAUDRATE        1000000U  /* 1 MHz */

/* CS Pin: PTA18 (J2 Pin 10) */
#define MFRC522_CS_GPIO             GPIOA
#define MFRC522_CS_PORT             PORTA
#define MFRC522_CS_PIN              18U

/* RST Pin: PTC4 (J1 Pin 6) */
#define MFRC522_RST_GPIO            GPIOC
#define MFRC522_RST_PORT            PORTC
#define MFRC522_RST_PIN             4U

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define MFRC522_CS_LOW()            GPIO_PinWrite(MFRC522_CS_GPIO, MFRC522_CS_PIN, 0U)
#define MFRC522_CS_HIGH()           GPIO_PinWrite(MFRC522_CS_GPIO, MFRC522_CS_PIN, 1U)
#define MFRC522_RST_LOW()           GPIO_PinWrite(MFRC522_RST_GPIO, MFRC522_RST_PIN, 0U)
#define MFRC522_RST_HIGH()          GPIO_PinWrite(MFRC522_RST_GPIO, MFRC522_RST_PIN, 1U)

/*******************************************************************************
 * Private Variables
 ******************************************************************************/
static SERIAL_MANAGER_WRITE_HANDLE_DEFINE(s_mfrcSerialWriteHandle);
static bool s_mfrcSerialInitialized = false;
static volatile bool s_mfrcBusy = false;

/*******************************************************************************
 * Private Helpers
 ******************************************************************************/
static void MFRC522_DelayMs(uint32_t ms)
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

static uint8_t MFRC522_SPI_Transfer(uint8_t data)
{
    lpspi_transfer_t xfer;
    uint8_t tx = data;
    uint8_t rx = 0;

    xfer.txData = &tx;
    xfer.rxData = &rx;
    xfer.dataSize = 1;
    xfer.configFlags = kLPSPI_MasterPcsContinuous;

    LPSPI_MasterTransferBlocking(MFRC522_SPI_BASE, &xfer);
    return rx;
}

static int MFRC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                          uint8_t *backData, uint8_t *backLen)
{
    int status = MFRC522_STATUS_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint32_t i;

    if (command == MFRC522_CMD_MF_AUTHENT)
    {
        irqEn = 0x12;
        waitIRq = 0x10;
    }
    else if (command == MFRC522_CMD_TRANSCEIVE)
    {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    MFRC522_WriteRegister(MFRC522_REG_COM_I_EN, irqEn | 0x80);
    MFRC522_ClearBitMask(MFRC522_REG_COM_IRQ, 0x80);
    MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);

    for (i = 0; i < sendLen; i++)
    {
        MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);
    }

    MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);

    if (command == MFRC522_CMD_TRANSCEIVE)
    {
        MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
    }

    i = 25000;
    do
    {
        n = MFRC522_ReadRegister(MFRC522_REG_COM_IRQ);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);

    if (i != 0)
    {
        if (!(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B))
        {
            status = MFRC522_STATUS_OK;

            if (n & irqEn & 0x01)
            {
                status = MFRC522_STATUS_NOTAG;
            }

            if (command == MFRC522_CMD_TRANSCEIVE)
            {
                n = MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);
                lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;

                if (lastBits != 0)
                {
                    *backLen = (n - 1) * 8 + lastBits;
                }
                else
                {
                    *backLen = n * 8;
                }

                if (n == 0)
                {
                    n = 1;
                }
                if (n > 16)
                {
                    n = 16;
                }

                for (i = 0; i < n; i++)
                {
                    backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);
                }
            }
        }
        else
        {
            status = MFRC522_STATUS_ERR;
        }
    }
    else
    {
        status = MFRC522_STATUS_TIMEOUT;
    }

    return status;
}

/*******************************************************************************
 * Public API
 ******************************************************************************/

uint8_t MFRC522_ReadRegister(uint8_t reg)
{
    uint8_t val;
    MFRC522_CS_LOW();
    /* Address byte for read: bit 7 = 1, bits 6:1 = address, bit 0 = 0 */
    MFRC522_SPI_Transfer(((reg << 1) & 0x7E) | 0x80);
    val = MFRC522_SPI_Transfer(0x00);
    MFRC522_CS_HIGH();
    return val;
}

void MFRC522_WriteRegister(uint8_t reg, uint8_t val)
{
    MFRC522_CS_LOW();
    /* Address byte for write: bit 7 = 0, bits 6:1 = address, bit 0 = 0 */
    MFRC522_SPI_Transfer((reg << 1) & 0x7E);
    MFRC522_SPI_Transfer(val);
    MFRC522_CS_HIGH();
}

void MFRC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t current = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, current | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t current = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, current & (~mask));
}

void MFRC522_Reset(void)
{
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET);
    MFRC522_DelayMs(50);
}

void MFRC522_AntennaOn(void)
{
    uint8_t temp = MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
    if ((temp & 0x03) != 0x03)
    {
        MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);
    }
}

void MFRC522_AntennaOff(void)
{
    MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

uint8_t MFRC522_GetVersion(void)
{
    return MFRC522_ReadRegister(MFRC522_REG_VERSION);
}

int MFRC522_Init(void)
{
    gpio_pin_config_t gpioConfig;
    lpspi_master_config_t masterConfig;
    uint8_t version;

    /* 1. Clocks: Enable clock for Port A, Port B, Port C, and LPSPI1 */
    CLOCK_EnableClock(kCLOCK_PortA);
    CLOCK_EnableClock(kCLOCK_PortB);
    CLOCK_EnableClock(kCLOCK_PortC);
    CLOCK_SetIpSrc(kCLOCK_Lpspi1, kCLOCK_IpSrcFro6M);

    /* 2. Deselect onboard SPI Flash (PTB0=PCS0, PTB4=PCS3) to prevent MISO contention */
    gpio_pin_config_t flashCsConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U
    };
    PORT_SetPinMux(PORTB, 0U, kPORT_MuxAsGpio);
    GPIO_PinInit(GPIOB, 0U, &flashCsConfig);
    PORT_SetPinMux(PORTB, 4U, kPORT_MuxAsGpio);
    GPIO_PinInit(GPIOB, 4U, &flashCsConfig);

    /* 3. GPIO Pin Muxing & Configuration:
     *    PTA18: Software CS (GPIO Output, Default HIGH)
     *    PTC4 : Hardware Reset (GPIO Output, Default HIGH)
     */
    PORT_SetPinMux(MFRC522_CS_PORT, MFRC522_CS_PIN, kPORT_MuxAsGpio);
    gpioConfig.pinDirection = kGPIO_DigitalOutput;
    gpioConfig.outputLogic = 1U;
    GPIO_PinInit(MFRC522_CS_GPIO, MFRC522_CS_PIN, &gpioConfig);

    PORT_SetPinMux(MFRC522_RST_PORT, MFRC522_RST_PIN, kPORT_MuxAsGpio);
    gpioConfig.pinDirection = kGPIO_DigitalOutput;
    gpioConfig.outputLogic = 1U;
    GPIO_PinInit(MFRC522_RST_GPIO, MFRC522_RST_PIN, &gpioConfig);

    /* 3. SPI Pin Muxing (LPSPI1 on Port B):
     *    PTB1 = LPSPI1_SIN (MISO, Alt 2)
     *    PTB2 = LPSPI1_SCK (SCK, Alt 2)
     *    PTB3 = LPSPI1_SOUT (MOSI, Alt 2)
     */
    PORT_SetPinMux(PORTB, 1U, kPORT_MuxAlt2);
    PORT_SetPinMux(PORTB, 2U, kPORT_MuxAlt2);
    PORT_SetPinMux(PORTB, 3U, kPORT_MuxAlt2);

    /* 4. Configure LPSPI1 Master */
    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = MFRC522_SPI_BAUDRATE;
    masterConfig.bitsPerFrame = 8U;
    masterConfig.cpol = kLPSPI_ClockPolarityActiveHigh;
    masterConfig.cpha = kLPSPI_ClockPhaseFirstEdge;
    masterConfig.direction = kLPSPI_MsbFirst;
    masterConfig.pcsToSckDelayInNanoSec = 100U;
    masterConfig.lastSckToPcsDelayInNanoSec = 100U;
    masterConfig.betweenTransferDelayInNanoSec = 50U;
    masterConfig.pinCfg = kLPSPI_SdiInSdoOut;
    masterConfig.pcsActiveHighOrLow = kLPSPI_PcsActiveLow;

    LPSPI_MasterInit(MFRC522_SPI_BASE, &masterConfig, CLOCK_GetIpFreq(kCLOCK_Lpspi1));

    /* 5. Hardware reset cycle */
    MFRC522_RST_LOW();
    MFRC522_DelayMs(10);
    MFRC522_RST_HIGH();
    MFRC522_DelayMs(50);

    /* 6. Soft reset */
    MFRC522_Reset();

    /* 7. Configure MFRC522 internal registers */
    MFRC522_WriteRegister(MFRC522_REG_T_MODE, 0x8D);
    MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0x3E);
    MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_HI, 0x00);
    MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_LO, 0x1E);

    MFRC522_WriteRegister(MFRC522_REG_TX_ASK, 0x40);
    MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);

    /* 8. Enable Antenna */
    MFRC522_AntennaOn();

    /* 9. Verify communication with chip */
    version = MFRC522_GetVersion();
    if (version == 0x00 || version == 0xFF)
    {
        return MFRC522_STATUS_ERR;
    }

    return MFRC522_STATUS_OK;
}

int MFRC522_Request(uint8_t reqMode, uint8_t *tagType)
{
    int status;
    uint8_t backBits = 0;

    MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);

    tagType[0] = reqMode;
    status = MFRC522_ToCard(MFRC522_CMD_TRANSCEIVE, tagType, 1, tagType, &backBits);

    if ((status != MFRC522_STATUS_OK) || (backBits != 0x10))
    {
        return MFRC522_STATUS_ERR;
    }

    return MFRC522_STATUS_OK;
}

int MFRC522_Anticoll(uint8_t *serNum)
{
    int status;
    uint8_t backBits = 0;

    MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);

    serNum[0] = PICC_CMD_SEL_CL1;
    serNum[1] = 0x20;

    status = MFRC522_ToCard(MFRC522_CMD_TRANSCEIVE, serNum, 2, serNum, &backBits);

    if (status == MFRC522_STATUS_OK)
    {
        uint8_t bcc = serNum[0] ^ serNum[1] ^ serNum[2] ^ serNum[3];
        if (bcc != serNum[4])
        {
            status = MFRC522_STATUS_ERR;
        }
    }

    return status;
}

void MFRC522_Halt(void)
{
    uint8_t buf[4];
    uint8_t backBits = 0;

    buf[0] = PICC_CMD_HLTA;
    buf[1] = 0;

    (void)MFRC522_ToCard(MFRC522_CMD_TRANSCEIVE, buf, 2, buf, &backBits);
}

bool MFRC522_CheckCard(uint8_t *uid)
{
    uint8_t tagType[2] = {0};
    uint8_t serNum[5] = {0};

    if (s_mfrcBusy)
    {
        return false;
    }
    s_mfrcBusy = true;

    if (MFRC522_Request(PICC_CMD_REQA, tagType) != MFRC522_STATUS_OK)
    {
        s_mfrcBusy = false;
        return false;
    }

    if (MFRC522_Anticoll(serNum) != MFRC522_STATUS_OK)
    {
        s_mfrcBusy = false;
        return false;
    }

    uid[0] = serNum[0];
    uid[1] = serNum[1];
    uid[2] = serNum[2];
    uid[3] = serNum[3];

    MFRC522_Halt();

    s_mfrcBusy = false;
    return true;
}

void MFRC522_SerialPrint(const char *str)
{
    if (str == NULL)
    {
        return;
    }

    if (!s_mfrcSerialInitialized)
    {
        extern uint32_t gSerMgrIf[];
        if (SerialManager_OpenWriteHandle((serial_handle_t)gSerMgrIf,
                                          (serial_write_handle_t)s_mfrcSerialWriteHandle) == kStatus_SerialManager_Success)
        {
            s_mfrcSerialInitialized = true;
        }
    }

    if (s_mfrcSerialInitialized)
    {
        (void)SerialManager_WriteBlocking((serial_write_handle_t)s_mfrcSerialWriteHandle,
                                          (uint8_t *)(uintptr_t)str,
                                          strlen(str));
    }
}
