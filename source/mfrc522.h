/*! *********************************************************************************
 * \file mfrc522.h
 *
 * MFRC522 RFID Reader Driver Header for NXP FRDM-MCXW71
 * Microcontroller: NXP MCXW716C (Arm Cortex-M33)
 * Communication  : LPSPI1 (Master, Mode 0, 1 MHz)
 *
 * Pin Connections:
 *   SCK  -> PTB2 (LPSPI1_SCK, Alt 2)
 *   MOSI -> PTB3 (LPSPI1_SOUT, Alt 2)
 *   MISO -> PTB1 (LPSPI1_SIN, Alt 2)
 *   SDA  -> PTA18 (GPIO Output, Software CS)
 *   RST  -> PTC4 (GPIO Output, Active Low Reset)
 *   3.3V -> 3V3
 *   GND  -> GND
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ********************************************************************************** */

#ifndef MFRC522_H
#define MFRC522_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * MFRC522 Registers
 ******************************************************************************/
/* Page 0: Command and Status */
#define MFRC522_REG_COMMAND           0x01
#define MFRC522_REG_COM_I_EN          0x02
#define MFRC522_REG_DIV_I_EN          0x03
#define MFRC522_REG_COM_IRQ           0x04
#define MFRC522_REG_DIV_IRQ           0x05
#define MFRC522_REG_ERROR             0x06
#define MFRC522_REG_STATUS1           0x07
#define MFRC522_REG_STATUS2           0x08
#define MFRC522_REG_FIFO_DATA         0x09
#define MFRC522_REG_FIFO_LEVEL        0x0A
#define MFRC522_REG_WATER_LEVEL       0x0B
#define MFRC522_REG_CONTROL           0x0C
#define MFRC522_REG_BIT_FRAMING       0x0D
#define MFRC522_REG_COLL              0x0E

/* Page 1: Command */
#define MFRC522_REG_MODE              0x11
#define MFRC522_REG_TX_MODE           0x12
#define MFRC522_REG_RX_MODE           0x13
#define MFRC522_REG_TX_CONTROL        0x14
#define MFRC522_REG_TX_ASK            0x15
#define MFRC522_REG_TX_SEL            0x16
#define MFRC522_REG_RX_SEL            0x17
#define MFRC522_REG_RX_THRESHOLD      0x18
#define MFRC522_REG_DEMOD             0x19
#define MFRC522_REG_MIFARE_TX         0x1C
#define MFRC522_REG_MIFARE_RX         0x1D
#define MFRC522_REG_SERIAL_SPEED      0x1F

/* Page 2: Configuration */
#define MFRC522_REG_CRC_RESULT_MSB    0x21
#define MFRC522_REG_CRC_RESULT_LSB    0x22
#define MFRC522_REG_MOD_WIDTH         0x24
#define MFRC522_REG_RF_CFG            0x26
#define MFRC522_REG_GS_N              0x27
#define MFRC522_REG_CW_GS_P           0x28
#define MFRC522_REG_MOD_GS_P          0x29
#define MFRC522_REG_T_MODE            0x2A
#define MFRC522_REG_T_PRESCALER       0x2B
#define MFRC522_REG_T_RELOAD_HI       0x2C
#define MFRC522_REG_T_RELOAD_LO       0x2D
#define MFRC522_REG_T_COUNTER_VAL_HI  0x2E
#define MFRC522_REG_T_COUNTER_VAL_LO  0x2F

/* Page 3: Test */
#define MFRC522_REG_TEST_SEL1         0x31
#define MFRC522_REG_TEST_SEL2         0x32
#define MFRC522_REG_TEST_PIN_EN       0x33
#define MFRC522_REG_TEST_PIN_VALUE    0x34
#define MFRC522_REG_TEST_BUS          0x35
#define MFRC522_REG_AUTO_TEST         0x36
#define MFRC522_REG_VERSION           0x37
#define MFRC522_REG_ANALOG_TEST       0x38
#define MFRC522_REG_TEST_DAC1         0x39
#define MFRC522_REG_TEST_DAC2         0x3A
#define MFRC522_REG_TEST_ADC          0x3B

/*******************************************************************************
 * MFRC522 Command Codes
 ******************************************************************************/
#define MFRC522_CMD_IDLE              0x00
#define MFRC522_CMD_MEM               0x01
#define MFRC522_CMD_GEN_RANDOM_ID     0x02
#define MFRC522_CMD_CALC_CRC          0x03
#define MFRC522_CMD_TRANSMIT          0x04
#define MFRC522_CMD_NO_CMD_CHANGE     0x07
#define MFRC522_CMD_RECEIVE           0x08
#define MFRC522_CMD_TRANSCEIVE        0x0C
#define MFRC522_CMD_MF_AUTHENT        0x0E
#define MFRC522_CMD_SOFT_RESET        0x0F

/*******************************************************************************
 * PICC Commands (ISO 14443-A)
 ******************************************************************************/
#define PICC_CMD_REQA                 0x26
#define PICC_CMD_WUPA                 0x52
#define PICC_CMD_CT                   0x88
#define PICC_CMD_SEL_CL1              0x93
#define PICC_CMD_SEL_CL2              0x95
#define PICC_CMD_SEL_CL3              0x97
#define PICC_CMD_HLTA                 0x50

/*******************************************************************************
 * Status and Return Codes
 ******************************************************************************/
#define MFRC522_STATUS_OK             0
#define MFRC522_STATUS_NOTAG          1
#define MFRC522_STATUS_ERR            2
#define MFRC522_STATUS_TIMEOUT        3

#define MFRC522_UID_SIZE              4

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

/*!
 * @brief Initialize the MFRC522 hardware (GPIOs, LPSPI1 clock & bus, and soft reset).
 *
 * @return MFRC522_STATUS_OK on success, MFRC522_STATUS_ERR if chip version check fails.
 */
int MFRC522_Init(void);

/*!
 * @brief Perform a soft reset of the MFRC522 chip.
 */
void MFRC522_Reset(void);

/*!
 * @brief Turn on the RF field antenna.
 */
void MFRC522_AntennaOn(void);

/*!
 * @brief Turn off the RF field antenna.
 */
void MFRC522_AntennaOff(void);

/*!
 * @brief Read a register from the MFRC522 via SPI.
 *
 * @param reg Register address (0x00 to 0x3F).
 * @return Register value.
 */
uint8_t MFRC522_ReadRegister(uint8_t reg);

/*!
 * @brief Write a value to an MFRC522 register via SPI.
 *
 * @param reg Register address (0x00 to 0x3F).
 * @param val Value to write.
 */
void MFRC522_WriteRegister(uint8_t reg, uint8_t val);

/*!
 * @brief Set bits in an MFRC522 register using a bitmask.
 *
 * @param reg Register address.
 * @param mask Bitmask to set.
 */
void MFRC522_SetBitMask(uint8_t reg, uint8_t mask);

/*!
 * @brief Clear bits in an MFRC522 register using a bitmask.
 *
 * @param reg Register address.
 * @param mask Bitmask to clear.
 */
void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask);

/*!
 * @brief Get the chip firmware version byte.
 *
 * @return Version byte (e.g., 0x91 for v1.0, 0x92 for v2.0).
 */
uint8_t MFRC522_GetVersion(void);

/*!
 * @brief Send a request command (REQA or WUPA) to detect cards in the RF field.
 *
 * @param reqMode Request mode (PICC_CMD_REQA or PICC_CMD_WUPA).
 * @param tagType Buffer to receive 2-byte ATQA response.
 * @return MFRC522_STATUS_OK if card responded, MFRC522_STATUS_ERR / NOTAG otherwise.
 */
int MFRC522_Request(uint8_t reqMode, uint8_t *tagType);

/*!
 * @brief Anti-collision detection to read the 4-byte card UID and verify BCC.
 *
 * @param serNum Buffer to store the 5-byte result (4-byte UID + 1-byte BCC checksum).
 * @return MFRC522_STATUS_OK on success, MFRC522_STATUS_ERR on error.
 */
int MFRC522_Anticoll(uint8_t *serNum);

/*!
 * @brief Check if a card is in the RF field, read its 4-byte UID, and halt it.
 *
 * @param uid Output buffer for the 4-byte UID.
 * @return true if a card was detected and UID read successfully, false otherwise.
 */
bool MFRC522_CheckCard(uint8_t *uid);

/*!
 * @brief Send PICC_CMD_HLTA command to place the active card into HALT state.
 */
void MFRC522_Halt(void);

/*!
 * @brief Print a string to the serial console using the MCXW71 Serial Manager.
 *
 * @param str Null-terminated string to output.
 */
void MFRC522_SerialPrint(const char *str);

#endif /* MFRC522_H */
