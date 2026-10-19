#ifndef __INT_W25Q64_H__
#define __INT_W25Q64_H__

#include "spi.h"
#include "gpio.h"

#define W25Q64_READ_ID 0x9F
#define W25Q64_WRITE_ENABLE 0x06
#define W25Q64_WRITE_DISABLE 0x04
#define W25Q64_READ_STATUS_REG1 0x05
#define W25Q64_READ_STATUS_REG2 0x35
#define W25Q64_WRITE_STATUS_REG1 0x01
#define W25Q64_WRITE_STATUS_REG2 0x31
#define W25Q64_READ_DATA 0x03
#define W25Q64_FAST_READ 0x0B
#define W25Q64_WRITE_DATA 0x02
#define W25Q64_SECTOR_ERASE 0x20
#define W25Q64_BLOCK_ERASE 0xD8
#define W25Q64_CHIP_ERASE 0xC7
#define W25Q64_POWER

void Int_w25q64_start(void);
void Int_w25q64_stop(void);

void Int_w25q64_write_byte(uint8_t data);
uint8_t Int_w25q64_read_byte(void);
void Int_w25q64_read_id(uint8_t *mf_id, uint16_t *device_id);
void Int_w25q64_read_data(uint32_t addr,uint8_t *data,uint16_t len);
void Int_w25q64_write_data_addr(uint32_t addr,uint8_t *data,uint16_t len);
void Int_w25q64_erase_sector(uint8_t block,uint8_t sector);

#endif
