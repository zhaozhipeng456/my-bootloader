#include "App_upgrade.h"
#include "string.h"

/* ==================== 内部状态机 ==================== */
typedef enum
{
    UPGRADE_STATE_WAIT_CMD,   /* 等待固定升级命令（空闲帧中断接收中） */
    UPGRADE_STATE_RECV_DATA,  /* 轮询接收固件数据并写入 W25Q64 */
    UPGRADE_STATE_ABORT       /* 出错，回到等待命令 */
} Upgrade_State;

/* ---------- 中断回调 -> 主循环 的交接变量 ---------- */
static volatile uint8_t  g_up_cmd_flag = 0;   /* 1：已收到固定升级命令 */
static volatile uint32_t g_fw_len      = 0;   /* 固件总长度（字节） */
static Upgrade_State g_up_state = UPGRADE_STATE_WAIT_CMD;

/* ---------- 固件写入 W25Q64 相关 ---------- */
static uint8_t  g_page_buf[256];              /* 页缓冲：攒满 256B 写一页 */
static uint32_t g_fw_offset = 0;              /* 已写入 W25Q64 的累计字节数 */
static uint16_t g_page_cnt  = 0;              /* 当前页缓冲中的字节数 */
static uint32_t g_fw_crc    = 0xFFFFFFFFUL;   /* 固件 CRC-32 累加器（写 W25Q64 时边写边算） */

/* ---------- 逐字节中断接收的环形缓冲 ---------- */
static uint8_t  g_ring[UPGRADE_RING_LEN];
static volatile uint16_t g_ring_head = 0;     /* 写指针（ISR 维护） */
static volatile uint16_t g_ring_tail = 0;     /* 读指针（主循环维护） */
static uint8_t  g_rx_byte = 0;                /* 单字节接收变量 */
static uint32_t g_last_rx_tick = 0;           /* 最近一次收到数据的时间 */

/* ---------- 命令帧接收缓冲（空闲帧中断接收用） ---------- */
static uint8_t g_cmd_buf[UPGRADE_CMD_BUF_LEN];

/* ===== 软件 CRC-32（IEEE 802.3，多项式 0x04C11DB7，初值 0xFFFFFFFF，输出取反）=====
 * 与 Bootloader 工程 Up_bootloader.c 中的 crc32_update 实现保持一致，两侧结果必须相同 */
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

/* 重新挂起空闲帧中断接收，继续等待固定命令 */
static void App_upgrade_rearm_cmd(void)
{
    memset(g_cmd_buf, 0, UPGRADE_CMD_BUF_LEN);
    /* 清空错误/空闲标志，防止历史残留干扰下一次接收 */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, g_cmd_buf, UPGRADE_CMD_BUF_LEN);
}

/**
 * @brief 空闲帧中断回调：检测固定升级命令
 * 只做判断和置位，不在这里做耗时操作（打印/擦除/写Flash都放到主循环）
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1)
        return;

    /* 不在等待命令状态时，忽略所有帧（此时也没挂起命令接收，不会进来） */
    if (g_up_state != UPGRADE_STATE_WAIT_CMD)
        return;

    /* 命令帧很短，正常情况下不可能占满整个缓冲区；占满了视为无效帧 */
    if (Size >= UPGRADE_CMD_BUF_LEN)
    {
        App_upgrade_rearm_cmd();
        return;
    }

    g_cmd_buf[Size] = 0;   /* 末尾补0，strstr 才能安全扫描 */

    char *p = strstr((char *)g_cmd_buf, UPGRADE_CMD_STR);
    if (p != NULL)
    {
        long l = atol((char *)p + strlen(UPGRADE_CMD_STR));
        if (l > 0 && l <= (long)UPGRADE_MAX_FW_LEN)
        {
            g_fw_len     = (uint32_t)l;
            g_up_cmd_flag = 1;
            return;   /* 匹配成功：不再重新挂起，串口交给逐字节中断接收 */
        }
    }

    /* 没匹配到固定命令：重新挂起，继续等 */
    App_upgrade_rearm_cmd();
}

/**
 * @brief 单字节接收中断回调：把字节塞进环形缓冲
 * 写 W25Q64 期间串口照常收数据，由环形缓冲兜底，避免丢字节
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
        return;

    uint16_t next = (g_ring_head + 1) % UPGRADE_RING_LEN;
    if (next != g_ring_tail)   /* 缓冲没满才写入，满了直接丢弃 */
    {
        g_ring[g_ring_head] = g_rx_byte;
        g_ring_head = next;
    }
    g_last_rx_tick = HAL_GetTick();

    /* 继续接收下一字节 */
    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
}

/* 轮询：把环形缓冲中的字节攒页并写入 W25Q64 */
static void App_upgrade_drain_ring(void)
{
    while (g_ring_head != g_ring_tail)
    {
        g_page_buf[g_page_cnt++] = g_ring[g_ring_tail];
        g_ring_tail = (g_ring_tail + 1) % UPGRADE_RING_LEN;
        g_fw_offset++;

        /* 攒满一页(256B) 或 已收完最后不足一页的部分 -> 写入 W25Q64 */
        if (g_page_cnt == 256 || g_fw_offset >= g_fw_len)
        {
            uint32_t page_start = W25Q64_FW_BASE + (g_fw_offset - g_page_cnt);
            Int_w25q64_write_data_addr(page_start, g_page_buf, g_page_cnt);
            /* 边写边算 CRC：喂入与 W25Q64 固件区完全相同的字节流 */
            for (uint16_t i = 0; i < g_page_cnt; i++)
                g_fw_crc = crc32_update(g_fw_crc, g_page_buf[i]);
            g_page_cnt = 0;
            if (g_fw_offset >= g_fw_len)
                break;
        }
    }
}

/* 把升级信息写入 W25Q64：0x10000 存固件地址，0x10004 存固件大小（小端） */
static void App_upgrade_write_meta(void)
{
    uint8_t meta[12];
    uint32_t fw_addr = W25Q64_FW_BASE;
    uint32_t fw_crc  = g_fw_crc ^ 0xFFFFFFFFUL;   /* 收尾取反，得到最终 CRC-32 */

    meta[0] = (uint8_t)(fw_addr & 0xFF);
    meta[1] = (uint8_t)((fw_addr >> 8) & 0xFF);
    meta[2] = (uint8_t)((fw_addr >> 16) & 0xFF);
    meta[3] = (uint8_t)((fw_addr >> 24) & 0xFF);
    meta[4] = (uint8_t)(g_fw_len & 0xFF);
    meta[5] = (uint8_t)((g_fw_len >> 8) & 0xFF);
    meta[6] = (uint8_t)((g_fw_len >> 16) & 0xFF);
    meta[7] = (uint8_t)((g_fw_len >> 24) & 0xFF);
    meta[8]  = (uint8_t)(fw_crc & 0xFF);
    meta[9]  = (uint8_t)((fw_crc >> 8) & 0xFF);
    meta[10] = (uint8_t)((fw_crc >> 16) & 0xFF);
    meta[11] = (uint8_t)((fw_crc >> 24) & 0xFF);

    Int_w25q64_write_data_addr(W25Q64_META_BASE, meta, 12);
    printf("meta: fw addr=0x%08lX size=%lu crc=0x%08lX\r\n",
           (unsigned long)fw_addr, (unsigned long)g_fw_len, (unsigned long)fw_crc);
}

/* 向内部 Flash 最后一页起始地址写入升级标志 0x55，并回读校验 */
static uint8_t App_upgrade_set_flag(void)
{
    uint32_t page_addr = UPGRADE_FLAG_ADDR - (UPGRADE_FLAG_ADDR % FLASH_PAGE_SIZE);

    __disable_irq();   /* 擦写内部 Flash 期间关中断，防止被串口/滴答打断 */
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase_init = {0};
    erase_init.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_init.Banks       = FLASH_BANK_1;
    erase_init.PageAddress = page_addr;
    erase_init.NbPages     = 1;
    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        __enable_irq();
        return 1;
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, UPGRADE_FLAG_ADDR,
                          (uint64_t)UPGRADE_FLAG_VALUE) != HAL_OK)
    {
        HAL_FLASH_Lock();
        __enable_irq();
        return 1;
    }
	printf("flag ok\n");
    HAL_FLASH_Lock();
    __enable_irq();

    /* 回读校验 */
    return (*(volatile uint8_t *)UPGRADE_FLAG_ADDR == UPGRADE_FLAG_VALUE) ? 0 : 1;
}

/**
 * @brief 初始化升级模块
 */
void App_upgrade_init(void)
{
    g_up_cmd_flag = 0;
    g_fw_len      = 0;
    g_up_state    = UPGRADE_STATE_WAIT_CMD;
    printf("app upgrade ready\r\n");
    App_upgrade_rearm_cmd();
}

/**
 * @brief 升级状态机：主循环周期调用
 */
void App_upgrade_work(void)
{
    switch (g_up_state)
    {
    case UPGRADE_STATE_WAIT_CMD:
		HAL_GPIO_WritePin(LED_GPIO_Port,LED_Pin,GPIO_PIN_RESET);
		HAL_Delay(200);
		HAL_GPIO_WritePin(LED_GPIO_Port,LED_Pin,GPIO_PIN_SET);
		HAL_Delay(200);
        if (g_up_cmd_flag)
        {
            g_up_cmd_flag = 0;
            printf("cmd ok, fw len=%lu\r\n", (unsigned long)g_fw_len);

            /* 先开启逐字节中断接收（进环形缓冲），
               这样擦除 W25Q64 期间串口来的数据也不会丢 */
            g_ring_head = 0;
            g_ring_tail = 0;
            g_fw_offset = 0;
            g_page_cnt  = 0;
            g_fw_crc    = 0xFFFFFFFFUL;   /* 每次升级重新累计固件 CRC */
            g_last_rx_tick = HAL_GetTick();
            (void)huart1.Instance->DR;            /* 读 DR 清 RXNE，清掉残留字节 */
            __HAL_UART_CLEAR_OREFLAG(&huart1);
            HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

            /* 擦除 W25Q64 固件区：第二块 0~(N-1) 扇区 */
            for (uint8_t s = 0; s < UPGRADE_ERASE_SECTORS; s++)
                Int_w25q64_erase_sector(2, s);
            /* 擦除元数据区：第一块第 0 扇区 */
            Int_w25q64_erase_sector(1, 0);

            printf("erase ok, recv start\r\n");
            g_up_state = UPGRADE_STATE_RECV_DATA;
        }
        break;

    case UPGRADE_STATE_RECV_DATA:
        /* 轮询：把环形缓冲里的数据尽量写入 W25Q64 */
        App_upgrade_drain_ring();

        if (g_fw_offset >= g_fw_len)
        {
            printf("recv ok %lu bytes\r\n", (unsigned long)g_fw_offset);

            /* 1. 写升级信息（固件地址 + 固件大小） */
            App_upgrade_write_meta();

            /* 2. 写升级标志 0x55 到内部 Flash 最后一页 */
            if (App_upgrade_set_flag() != 0)
            {
                printf("set upgrade flag fail\r\n");
                g_up_state = UPGRADE_STATE_ABORT;
                break;
            }

            printf("upgrade done, system reset\r\n");
            HAL_Delay(100);
            NVIC_SystemReset();   /* 复位后由 Bootloader 检测 0x55 执行升级 */
        }
        else if ((HAL_GetTick() - g_last_rx_tick) > UPGRADE_RECV_TIMEOUT)
        {
            printf("recv timeout %lu/%lu\r\n",
                   (unsigned long)g_fw_offset, (unsigned long)g_fw_len);
            g_up_state = UPGRADE_STATE_ABORT;
        }
        break;

    case UPGRADE_STATE_ABORT:
        printf("abort, wait cmd again\r\n");
        HAL_UART_AbortReceive(&huart1);   /* 停止逐字节接收 */
        g_ring_head = 0;
        g_ring_tail = 0;
        g_fw_offset = 0;
        g_page_cnt  = 0;
        g_up_state = UPGRADE_STATE_WAIT_CMD;
        App_upgrade_rearm_cmd();
        break;

    default:
        break;
    }
}
