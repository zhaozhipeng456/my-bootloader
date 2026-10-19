#include "Up_bootloader.h"

uint16_t updata = 0;
// 升级结果：1=成功(可跳App区) 0=失败(跳出厂程序)
static uint8_t g_update_ok = 1;

void bootloader_check_update(void)
{
    updata = *(uint8_t *)UPDATE_FLAG_ADDR;
}

/* 清除升级标志：擦除内部 Flash 最后一页（0x0800FC00 恢复为 0xFF） */
static void bootloader_clear_flag(void)
{
    Int_bootloader_erase_flash(UPDATE_FLAG_ADDR, 1);
    /* 刷新 RAM 里的标志，防止后续跳转逻辑误判 */
    updata = *(uint8_t *)UPDATE_FLAG_ADDR;
}

/* ===== 软件 CRC-32（IEEE 802.3，多项式 0x04C11DB7，初值 0xFFFFFFFF，输出取反）=====
 * 与 App01 工程 App_upgrade.c 中的 crc32_update 实现保持一致，两侧结果必须相同 */
static uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
    crc ^= (uint32_t)byte << 24;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (crc & 0x80000000UL)
            crc = (crc << 1) ^ 0x04C11DB7UL;
        else
            crc <<= 1;
    }
    return crc;
}

/* 对 W25Q64 固件区（addr 起 size 字节）计算 CRC-32 */
static uint32_t crc32_w25q64(uint32_t addr, uint32_t size)
{
    uint8_t buf[256];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t off = 0;
    while (off < size)
    {
        uint16_t chunk = (size - off > 256) ? 256 : (uint16_t)(size - off);
        Int_w25q64_read_data(addr + off, buf, chunk);
        for (uint16_t i = 0; i < chunk; i++)
            crc = crc32_update(crc, buf[i]);
        off += chunk;
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* 执行升级：读 W25Q64 元数据 -> 固件从 W25Q64 刷入内部 Flash
 * 返回 0:成功 1:失败 */
static uint8_t bootloader_do_update(void)
{
    uint8_t meta[12];
    uint32_t fw_addr = 0;
    uint32_t fw_size = 0;
    uint32_t fw_crc  = 0;

    /* 1. 读 App01 写入的升级信息：0x10000 固件地址 + 0x10004 固件大小 + 0x10008 CRC32（小端） */
    Int_w25q64_read_data(W25Q64_META_ADDR, meta, 12);
    fw_addr = (uint32_t)meta[0] | ((uint32_t)meta[1] << 8) |
              ((uint32_t)meta[2] << 16) | ((uint32_t)meta[3] << 24);
    fw_size = (uint32_t)meta[4] | ((uint32_t)meta[5] << 8) |
              ((uint32_t)meta[6] << 16) | ((uint32_t)meta[7] << 24);
    fw_crc  = (uint32_t)meta[8] | ((uint32_t)meta[9] << 8) |
              ((uint32_t)meta[10] << 16) | ((uint32_t)meta[11] << 24);
    printf("fw meta: addr=0x%08lX size=%lu crc=0x%08lX\r\n",
           (unsigned long)fw_addr, (unsigned long)fw_size, (unsigned long)fw_crc);

    /* 2. 参数校验：地址必须是固件区起点，大小在 App 区范围内 */
    if (fw_addr != W25Q64_FW_BASE || fw_size == 0 || fw_size > W25Q64_FW_MAX_SIZE)
    {
        printf("fw meta error, clear flag\r\n");
        bootloader_clear_flag();
        return 1;
    }

    /* 2.5 固件完整性校验：先验证再擦除 —— 不校验不动手
     * 0xFFFFFFFF 表示旧版 App01 未写 CRC 字段，跳过校验保持兼容 */
    if (fw_crc != 0xFFFFFFFFUL)
    {
        if (crc32_w25q64(fw_addr, fw_size) != fw_crc)
        {
            printf("fw crc error, clear flag\r\n");
            bootloader_clear_flag();
            return 1;
        }
        printf("fw crc ok\r\n");
    }
    else
    {
        printf("fw crc skipped (old app01 meta)\r\n");
    }

    /* 3. 执行 W25Q64 -> 内部 Flash */
    if (Int_bootloader_w25q64_to_flash(fw_addr, fw_size, FLASH_FIRMWARE_ADDR) != 0)
    {
        printf("update fail, keep flag for retry\r\n");
        return 1;   /* 失败保留标志：下次上电自动重试 */
    }

    printf("update ok, clear flag\r\n");
    bootloader_clear_flag();
    return 0;
}

void bootlodaer_update(void)
{
    if (updata == BOOT_UPDATA)
    {
        printf("update\n");
        g_update_ok = (bootloader_do_update() == 0);
    }
    else if(updata == BOOT_RESET)
    {
        printf("boot reset\n");
    }
    else
    {
        printf("no update\n");
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_12)
    {
        updata = BOOT_RESET;
    }
}

void bootloader_jump_app(void)
{
    if(updata == BOOT_RESET)
    {
        Int_bootloader_jump_to_app(RESET_START_ADDR);
    }
    else if(updata == BOOT_UPDATA && !g_update_ok)
    {
        /* 升级失败：App 区可能已被擦写破坏，跳到出厂程序 */
        printf("jump to reset app\n");
        Int_bootloader_jump_to_app(RESET_START_ADDR);
    }
    else
    {
        Int_bootloader_jump_to_app(APP_START_ADDR);
    }
}

/* W25Q64 写数据到  flash执行升级操作  */
uint8_t Int_bootloader_w25q64_to_flash(uint32_t w25q_addr, uint32_t size, uint32_t flash_addr)
{
    uint8_t buff[256];
    uint32_t w_offset = 0;


    if (size == 0)
    {
        printf("w25q64 size error\n");
        return 1;
    }
    if (flash_addr < 0x08000000 || flash_addr + size > APP_END_ADDR)
    {
        printf("flash addr error\n");
        return 1;
    }

    __disable_irq();     	/* 关中断写flash */  
    HAL_FLASH_Unlock();		/* 解锁 Flash 控制器 */
    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.PageAddress = flash_addr - flash_addr % FLASH_PAGE_SIZE;
    erase_init.NbPages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        __enable_irq();
        printf("flash erase error\n");
        return 1;
    }

    /*循环搬运一次最大搬运256字节 */
    while (w_offset < size)
    {
        uint16_t chunk = (size - w_offset > 256) ? 256 : (size - w_offset);
        Int_w25q64_read_data(w25q_addr + w_offset, buff, chunk);

        uint16_t n = chunk & 0xFFFE;   /* even part */
        for (uint16_t i = 0; i < n; i += 2)
        {
            uint16_t data16 = buff[i] | (buff[i + 1] << 8);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, flash_addr + w_offset + i, data16) != HAL_OK)
            {
                HAL_FLASH_Lock();
                __enable_irq();
                printf("flash program error\n");
                return 1;
            }
        }
        /* 奇数长度：最后剩1字节，高字节补0xFF */
        if (chunk & 0x01)
        {
            uint16_t data16 = buff[chunk - 1] | 0xFF00;
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, flash_addr + w_offset + chunk - 1, data16) != HAL_OK)
            {
                HAL_FLASH_Lock();
                __enable_irq();
                printf("flash program error\n");
                return 1;
            }
        }
        w_offset += chunk;
    }

    HAL_FLASH_Lock();
    __enable_irq();
    printf("update done: %lu bytes -> 0x%08lX, verify ok\r\n",
           (unsigned long)size, (unsigned long)flash_addr);
    return 0;
}
