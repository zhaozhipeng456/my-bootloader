#include "App_bootloader.h"


uint8_t app_rec_start_buff[64];
uint16_t app_rec_start_len = 0;
uint32_t app_rec_total_len = 0;

extern uint32_t last_rec_time;
extern uint16_t uart_rec_full_len;
Bootloader_status boot_status = BOOTLOADER_STATUS_INIT;

void App_bootloader_init(void)
{
    printf("reset start\r\n");
    printf("send 'start:len' to start\r\n");
    boot_status = BOOTLOADER_STATUS_INIT;
}

void App_bootloader_run(void)
{
    HAL_UARTEx_ReceiveToIdle(&huart1,app_rec_start_buff,64,&app_rec_start_len,0xffffff);
    if(app_rec_start_len > 0)
    {
        char *start_str = strstr((char*)app_rec_start_buff,"start:");
        if(start_str != NULL)
        {
            app_rec_total_len = atoi((char*)start_str + 6);
            if(app_rec_total_len > 0)
            {
                printf("app:len %lu\r\n",app_rec_total_len);
                boot_status = BOOTLOADER_STATUS_RUN;
            }
            else
            {
                printf("len error\r\n");
            }
        }
        else
        {
            printf("data error\r\n");
        }
    }

}

void App_bootloader_rec_data(void)
{
    // 只有真正收到过数据(last_rec_time != 0)才判超时，防止"一包都没收到也误报 rec ok"
    if(last_rec_time != 0 && (HAL_GetTick()-last_rec_time >2000))
    {
        boot_status = BOOTLOADER_STATUS_CHECK_DATA;
        printf("rec ok\r\n");
    }
}

uint8_t App_bootloader_check_data(void)
{
    if(uart_rec_full_len == app_rec_total_len)
    {
        boot_status = BOOTLOADER_STATUS_JUMP_APP;
        printf("chaeck ok\r\n");
        return 0;
    }
    else
    {
        printf("app rec error: %d/%d\r\n", uart_rec_full_len, app_rec_total_len);
    }
    return 1;
}

uint8_t App_bootloader_jump_app(void)
{
    printf("jump to app\r\n");
    uint8_t ret = Int_bootloader_jump_to_app();
    return ret;
}

//状态机
void App_bootloader_work(void)
{
    switch(boot_status)
    {
    case BOOTLOADER_STATUS_INIT:
        App_bootloader_run();
        break;
    case BOOTLOADER_STATUS_RUN:
    {
        if (app_rec_total_len > (APP_END_ADDR - APP_START_ADDR))
        {
            printf("len too large\r\n");
            NVIC_SystemReset();
        }
        Int_bootloader_erase_flash(APP_START_ADDR,31);
        printf("flash erase ok\r\n");
        printf("ready to receive app\r\n");
        boot_status = BOOTLOADER_STATUS_REC_DATA;
        Int_bootloader_receive_app();
        break;
    }
    case BOOTLOADER_STATUS_REC_DATA:
        App_bootloader_rec_data();
        break;
    case BOOTLOADER_STATUS_CHECK_DATA:
        if(App_bootloader_check_data())
        {
            printf("app rec error, system reset\r\n");
            NVIC_SystemReset();
        }
        break;
    case BOOTLOADER_STATUS_JUMP_APP:
        if(App_bootloader_jump_app())
        {
            printf("jump app error, system reset\r\n");
            NVIC_SystemReset();
        }
        break;

    default:
        break;
    }
}
