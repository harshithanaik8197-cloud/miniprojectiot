/*! *********************************************************************************
 * \file rc522_drv.c
 *
 * RC522 RFID Reader Driver Compatibility Wrapper
 * Delegates directly to the MFRC522 driver (source/mfrc522.c).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ********************************************************************************** */

#include "rc522_drv.h"
#include "mfrc522.h"

int RC522_Init(void)
{
    return (MFRC522_Init() == MFRC522_STATUS_OK) ? RC522_OK : RC522_ERR;
}

void RC522_WriteRegister(uint8_t reg, uint8_t val)
{
    MFRC522_WriteRegister(reg, val);
}

uint8_t RC522_ReadRegister(uint8_t reg)
{
    return MFRC522_ReadRegister(reg);
}

void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    MFRC522_SetBitMask(reg, mask);
}

void RC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
    MFRC522_ClearBitMask(reg, mask);
}

void RC522_AntennaOn(void)
{
    MFRC522_AntennaOn();
}

void RC522_AntennaOff(void)
{
    MFRC522_AntennaOff();
}

void RC522_Reset(void)
{
    MFRC522_Reset();
}

uint8_t RC522_GetVersion(void)
{
    return MFRC522_GetVersion();
}

int RC522_Request(uint8_t *atqa)
{
    return (MFRC522_Request(PICC_CMD_REQA, atqa) == MFRC522_STATUS_OK) ? RC522_OK : RC522_ERR;
}

int RC522_Anticoll(uint8_t *uid)
{
    return (MFRC522_Anticoll(uid) == MFRC522_STATUS_OK) ? RC522_OK : RC522_ERR;
}

void RC522_HaltA(void)
{
    MFRC522_Halt();
}

bool RC522_DetectCard(uint8_t uid[RC522_UID_SIZE])
{
    return MFRC522_CheckCard(uid);
}
