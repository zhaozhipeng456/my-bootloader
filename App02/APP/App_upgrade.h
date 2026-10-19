#ifndef __APP_UPGRADE_H
#define __APP_UPGRADE_H

#include "int_w25q64.h"
#include "usart.h"

/* ==================== 升级协议配置 ==================== */
/* 固定升级命令：串口收到 "start:" + 固件长度(十进制) 即进入升级流程 */
#define UPGRADE_CMD_STR          "start:"
/* 命令帧接收缓冲区大小（ReceiveToIdle 中断接收用） */
#define UPGRADE_CMD_BUF_LEN      64U
/* 固件接收超时（ms）：超过该时间没收到新数据判定失败 */
#define UPGRADE_RECV_TIMEOUT     5000U

/* ==================== W25Q64 内存布局（块=64KB，扇区=4KB，页=256B） ==================== */
/* 固件数据存放起始地址：第二块第一扇第一页 = 0x00020000 */
#define W25Q64_FW_BASE           (2U << 16)
/* 升级信息存放起始地址：第一块第一扇第一页 = 0x00010000 */
#define W25Q64_META_BASE         (1U << 16)
/* 固件大小存放地址：紧随地址之后 = 0x00010004 */
#define W25Q64_META_SIZE_OFF     4U
/* 固件 CRC32 存放地址：紧随大小之后 = 0x00010008（新增；旧版 App01 不写，Bootloader 会跳过校验） */
#define W25Q64_META_CRC_OFF      8U

/* 固件区擦除范围：第二块 0~(N-1) 扇区（N*4KB） */
#define UPGRADE_ERASE_SECTORS    8U
/* 固件长度上限：受 STM32F103C8 内部 Flash App 区限制
   （0x08008000 ~ 0x0800FC00 = 31KB，且 ≤ 擦除范围 32KB） */
#define UPGRADE_MAX_FW_LEN       (31U * 1024U)

/* ==================== 升级标志（内部 Flash） ==================== */
/* 与 Bootloader 工程 Up_bootloader.h 的 UPDATE_FLAG_ADDR 保持一致 */
#define UPGRADE_FLAG_ADDR        0x0800FC00U   /* STM32 内部 Flash 最后一页起始地址 */
#define UPGRADE_FLAG_VALUE       0x55U         /* 升级标志 */

/* ==================== 固件数据接收环形缓冲 ==================== */
#define UPGRADE_RING_LEN         512U

/**
 * @brief 初始化升级模块：开始空闲帧中断接收，等待固定升级命令
 */
void App_upgrade_init(void);

/**
 * @brief 升级状态机，主循环中周期调用（非阻塞）
 */
void App_upgrade_work(void);

#endif
