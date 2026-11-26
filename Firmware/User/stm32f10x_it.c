#include "stm32f10x_it.h"
#include "bsp.h"

/* 串口一接收中断入口，交给板级驱动读取接收数据。 */
void USART1_IRQHandler(void)
{
    UART1_ReceiveIrq();
}


void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1)
    {
    }
}
#endif

