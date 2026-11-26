#include "stm32f10x.h"

#include "app_tasks.h"

/* 程序入口：完成应用初始化并启动实时任务调度。 */
int main(void)
{
    App_Run();
    return 0;
}

