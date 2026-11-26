#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef int portBASE_TYPE;
typedef uint32_t portTickType;
#define pdFALSE 0
#define portEND_SWITCHING_ISR(value) ((void)(value))
static volatile uint8_t s_lcdSelection, s_pageArmed, s_pageReleaseTicks, s_pageRequestPending;
static volatile portTickType s_pageRequestTick;
static unsigned wakes;
static int s_displayWake;
static struct { uint32_t PR, IMR; } extiRegisters;
#define EXTI (&extiRegisters)
#define EXTI3_IRQn 9
static unsigned irqEnabled;
static void NVIC_ClearPendingIRQ(int irq) { (void)irq; }
static void NVIC_EnableIRQ(int irq) { (void)irq; irqEnabled=1; }
static void NVIC_DisableIRQ(int irq) { (void)irq; irqEnabled=0; }
static portTickType xTaskGetTickCountFromISR(void) { return 123U; }
static int xSemaphoreGiveFromISR(int sem, int *woken) { (void)sem; *woken=1; ++wakes; return 1; }
/* CURRENT_PAGE_KEY_FUNCTIONS */
/* CURRENT_PAGE_IRQ_HANDLER */
int main(void)
{
    unsigned sample;
    s_pageArmed=1;
    EXTI->IMR=8U;EXTI->PR=8U;irqEnabled=1;
    EXTI3_IRQHandler();
    assert(!irqEnabled && (EXTI->IMR&8U)==0);
    assert(s_lcdSelection==1 && wakes==1 && s_pageRequestPending && s_pageRequestTick==123);
    for(sample=0;sample<1000U;++sample)Page_UpdateRelease(8);
    assert(!s_pageArmed && !irqEnabled && (EXTI->IMR&8U)==0 && wakes==1);
    App_PagePressedIrq();
    assert(s_lcdSelection==1 && wakes==1);
    Page_UpdateRelease(0); Page_UpdateRelease(0);
    App_PagePressedIrq();
    assert(s_lcdSelection==1 && wakes==1);
    Page_UpdateRelease(8);
    Page_UpdateRelease(0); Page_UpdateRelease(0);
    assert(!s_pageArmed);
    Page_UpdateRelease(0); App_PagePressedIrq();
    assert(s_lcdSelection==2 && wakes==2 && !s_pageArmed && irqEnabled && (EXTI->IMR&8U));
    Page_UpdateRelease(0); Page_UpdateRelease(0); Page_UpdateRelease(0); App_PagePressedIrq();
    assert(s_lcdSelection==0 && wakes==3);
    s_lcdSelection=3;
    Page_UpdateRelease(0); Page_UpdateRelease(0); Page_UpdateRelease(0); App_PagePressedIrq();
    assert(s_lcdSelection==0 && wakes==4);
    puts("first IRQ wakes and masks line/NVIC; held/bouncing key is ignored; stable release rearms IRQ; page wrap PASS");
    return 0;
}
