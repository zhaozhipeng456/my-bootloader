#ifndef __UP_BOOTLOADER_H
#define __UP_BOOTLOADER_H

#include "int_bootloader.h"
#include "int_w25q64.h"
//升级标志位地址
#define UPDATE_FLAG_ADDR 0x800FC00  // chip flash last page (0x0800FC00-0x08010000)

//更新标志
#define BOOT_UPDATA 0x55
//恢复出厂设置
#define BOOT_RESET 0xAA

//判断当前是否需要更新
extern uint16_t updata;
void bootloader_check_update(void);
void bootlodaer_update(void);
void bootloader_jump_app(void);

// ===== W25Q64 升级功能（App01 运行期写入，Bootloader 读取执行）=====
// W25Q64 中升级信息（元数据）地址：第一块第一扇第一页 = 0x00010000
// 布局：0x10000 固件数据地址(4B, 小端) + 0x10004 固件大小(4B, 小端) + 0x10008 固件CRC32(4B, 小端)
#define W25Q64_META_ADDR        0x00010000
#define W25Q64_META_SIZE_ADDR   (W25Q64_META_ADDR + 4)
#define W25Q64_META_CRC_ADDR    (W25Q64_META_ADDR + 8)
// W25Q64 中固件数据起始地址（第二块第一扇第一页 = 0x00020000）
#define W25Q64_FW_BASE          0x00020000
// 固件大小上限：受 STM32F103C8 内部 Flash App 区限制（0x08008000 ~ 0x0800FC00）
#define W25Q64_FW_MAX_SIZE      (UPDATE_FLAG_ADDR - APP_START_ADDR)
// 写入芯片 Flash 的固定位置（App 区起始）
#define FLASH_FIRMWARE_ADDR     APP_START_ADDR

/**
 * @brief 从 W25Q64 读取固件数据写入芯片内部 Flash
 * @param w25q_addr   W25Q64 数据起始地址
 * @param size        数据字节数
 * @param flash_addr  芯片 Flash 目标起始地址
 * @return 0:成功 1:失败
 */
uint8_t Int_bootloader_w25q64_to_flash(uint32_t w25q_addr, uint32_t size, uint32_t flash_addr);


#endif
