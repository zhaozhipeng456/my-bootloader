#include "int_w25q64.h"

void Int_w25q64_start(void)
{
    HAL_GPIO_WritePin(GPIOA, W25Q64_CS_Pin, GPIO_PIN_RESET);
}
void Int_w25q64_stop(void)
{
    HAL_GPIO_WritePin(GPIOA, W25Q64_CS_Pin, GPIO_PIN_SET);
}
void Int_w25q64_write_byte(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1, &data, 1,100);
}
uint8_t Int_w25q64_read_byte(void)
{
    uint8_t data = 0;
    HAL_SPI_Receive(&hspi1, &data, 1, 1000);
    return data;
}

//等待忙状态
static void Int_w25q64_wait_busy(void)
{
    Int_w25q64_start();
    while(1)
    {
        Int_w25q64_write_byte(W25Q64_READ_STATUS_REG1);
        //找到最低位，为0表示不忙
        if((Int_w25q64_read_byte()&0x01) == 0)
        {
            break;
        }
    }
    Int_w25q64_stop();
}

//写使能
static void Int_w25q64_write_enable(void)
{
    Int_w25q64_wait_busy();
    Int_w25q64_start();
    Int_w25q64_write_byte(W25Q64_WRITE_ENABLE);
    Int_w25q64_stop();
}

void Int_w25q64_read_id(uint8_t *mf_id, uint16_t *device_id)
{
    Int_w25q64_start();
    Int_w25q64_write_byte(W25Q64_READ_ID);
    *mf_id = Int_w25q64_read_byte();
    uint8_t high = Int_w25q64_read_byte();
    uint8_t low = Int_w25q64_read_byte();
    *device_id = high << 8 | low;
    Int_w25q64_stop();
}

//写数据
void Int_w25q64_write_data_addr(uint32_t addr,uint8_t *data,uint16_t len)
{
    uint16_t idx = 0;
    while (len > 0)
    {
        uint16_t chunk = 256 - (addr & 0xFF);
        if (chunk > len) chunk = len;

        Int_w25q64_write_enable();
        Int_w25q64_start();
        Int_w25q64_write_byte(W25Q64_WRITE_DATA);
        Int_w25q64_write_byte((addr >> 16) & 0xFF);
        Int_w25q64_write_byte((addr >> 8) & 0xFF);
        Int_w25q64_write_byte(addr & 0xFF);
        for (uint16_t i = 0; i < chunk; i++)
        {
            Int_w25q64_write_byte(data[idx + i]);
        }
        Int_w25q64_stop();
        Int_w25q64_wait_busy();

        idx += chunk;
        addr += chunk;
        len -= chunk;
    }
}

//读数据
void Int_w25q64_read_data(uint32_t addr,uint8_t *data,uint16_t len)
{
    Int_w25q64_start();
    Int_w25q64_write_byte(W25Q64_READ_DATA);
    Int_w25q64_write_byte((addr >> 16) & 0xFF);
    Int_w25q64_write_byte((addr >> 8) & 0xFF);
    Int_w25q64_write_byte(addr & 0xFF);
    HAL_SPI_Receive(&hspi1, data, len, 1000);
    Int_w25q64_stop();
}


//擦除扇区
void Int_w25q64_erase_sector(uint8_t block,uint8_t sector)
{
    Int_w25q64_write_enable();
    Int_w25q64_start();
    Int_w25q64_write_byte(W25Q64_SECTOR_ERASE);
    Int_w25q64_write_byte(block);
    Int_w25q64_write_byte(sector <<4);
    Int_w25q64_write_byte(0);
    Int_w25q64_stop();
}


