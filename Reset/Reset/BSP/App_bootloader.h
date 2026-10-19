#ifndef __APP_BOOTLOADER_H
#define __APP_BOOTLOADER_H

#include "int_bootloader.h"

typedef enum
{
    BOOTLOADER_STATUS_INIT,
    BOOTLOADER_STATUS_RUN,
    BOOTLOADER_STATUS_REC_DATA,
    BOOTLOADER_STATUS_CHECK_DATA,
    BOOTLOADER_STATUS_JUMP_APP
}Bootloader_status;

void App_bootloader_init(void);

void App_bootloader_run(void);

void App_bootloader_rec_data(void);

uint8_t App_bootloader_check_data(void);

uint8_t App_bootloader_jump_app(void);

void App_bootloader_work(void);
#endif
