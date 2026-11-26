#include "stm32f10x_it.h"
#include "bsp.h"
#include "app_tasks.h"

/* 首次下降沿后屏蔽该中断，稳定释放时再由输入任务重新使能。
 * 按住期间不允许反复进入中断，否则低优先级显示任务可能无法运行。 */
void EXTI3_IRQHandler(void)
{
    EXTI->IMR &= ~8U;
    NVIC_DisableIRQ(EXTI3_IRQn);
    if ((EXTI->PR & 8U) != 0U)
    {
        EXTI->PR = 8U;
        App_PagePressedIrq();
    }
    NVIC_ClearPendingIRQ(EXTI3_IRQn);
}

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

