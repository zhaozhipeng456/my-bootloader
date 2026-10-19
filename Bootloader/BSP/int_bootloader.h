#ifndef __INT_BOOTLOADER_H
#define __INT_BOOTLOADER_H

#include "usart.h"
#include "stdlib.h"
#include "string.h"

#define BOOTLOADER_UART_REC_BUFF_LEN 512
//出厂程序起始地址 
#define RESET_START_ADDR 0x08004000
//A程序起始位置
#define APP_START_ADDR 0x08008000
//A程序结束位置（F103C8 只有 64KB：0x08000000~0x0800FFFF，最后一页 0x0800FC00 留给升级标志）
#define APP_END_ADDR 0x0800FC00
#define STACK_ADDR 0X20000000

uint8_t Int_bootloader_jump_to_app(uint32_t app_start_addr);

//外部可调用 提前擦除flash空间
void Int_bootloader_erase_flash(uint32_t page_addr, uint16_t pages);


#endif
