/* 128×64 中文 OLED 驱动。沿用原液晶接口名，减少应用层改动。
 * PB0 为时钟、PB1 为数据，均为开漏输出；外接 4.7k 上拉。
 * 字符串使用 UTF-8，中文采用固化的 16×16 点阵，无需外部字库。 */
#include "lcd1602.h"
#include "stm32f10x.h"
#include "bsp.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "oled_font.h"
static unsigned char screen[1024];
static volatile unsigned char rowBuffer[256];
static unsigned char busReady;
static unsigned char cursorRow;
static char textBuffer[64];
/* 缓存上一帧文本，内容未变时跳过字模查找与点阵生成。 */
static char lastLines[4][64];
#define SCL(v) do { if(v) GPIOB->BSRR=GPIO_Pin_0; else GPIOB->BRR=GPIO_Pin_0; } while(0)
#define SDA(v) do { if(v) GPIOB->BSRR=GPIO_Pin_1; else GPIOB->BRR=GPIO_Pin_1; } while(0)
static void PauseBus(void) { __NOP(); __NOP(); __NOP(); __NOP(); }
static void StartBus(void) { SDA(1);SCL(1);PauseBus();SDA(0);PauseBus();SCL(0); }
static void StopBus(void) { SDA(0);SCL(1);PauseBus();SDA(1);PauseBus(); }
static unsigned char SendByte(unsigned char v)
{
 unsigned char i,ack;
 for(i=0;i<8;i++){SDA(v&128);PauseBus();SCL(1);PauseBus();SCL(0);v<<=1;}
 SDA(1);PauseBus();SCL(1);PauseBus();ack=(GPIOB->IDR&GPIO_Pin_1)==0;SCL(0);return ack;
}
static void Command(unsigned char c) { StartBus();SendByte(0x78);SendByte(0);SendByte(c);StopBus(); }
/* 一次命令事务设置页与起始列，仅发送发生变化的连续列区间。 */
static void WriteRange(unsigned char page, unsigned char first,
                       unsigned char last, const unsigned char *data)
{
 unsigned int x;
 StartBus();SendByte(0x78);SendByte(0);
 SendByte(0xB0|page);SendByte(first&15U);SendByte(0x10|(first>>4));StopBus();
 StartBus();SendByte(0x78);SendByte(0x40);
 for(x=first;x<=last;x++)SendByte(data[x]);
 StopBus();
}
unsigned char LCD_BusReady(void) { return busReady; }
void LCD_Clear(void)
{
 unsigned char p;
 { unsigned int n; for(n=0;n<sizeof(screen);n++)((volatile unsigned char *)screen)[n]=0; }
 for(p=0;p<8;p++)WriteRange(p,0,127,screen+p*128);
 for(p=0;p<4;p++)lastLines[p][0]=0;
}
void LCD_Init(void)
{
 GPIO_InitTypeDef io;
 static const unsigned char init[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0,0x40,0x8D,0x14,0x20,0x02,0xA1,0xC8,0xDA,0x12,0x81,0xCF,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF};
 unsigned char i;
 io.GPIO_Pin=GPIO_Pin_0|GPIO_Pin_1;io.GPIO_Mode=GPIO_Mode_Out_OD;io.GPIO_Speed=GPIO_Speed_50MHz;GPIO_Init(GPIOB,&io);
 SDA(1);SCL(1);BSP_DelayMs(30);
 StartBus();busReady=SendByte(0x78);StopBus();
 for(i=0;i<sizeof(init);i++)Command(init[i]);
 LCD_Clear();
}
/* 逐行生成点阵，只刷新变化的页，降低低优先级显示任务的总线开销。 */
void LCD_PrintLine(uint8_t row,const char *text)
{
 const unsigned char *s=(const unsigned char *)text,*bitmap;
 unsigned int code,i,j,x=0,w,k;
 unsigned int first,last;
 if(row>=4)return;
 /* 明确逐字节比较，兼容本机仿真器的访存行为。 */
 for(k=0;k<63;k++){
  if((unsigned char)lastLines[row][k]!=(unsigned char)text[k])break;
  if(text[k]==0)return;
 }
 for(k=0;k<63 && text[k];k++)lastLines[row][k]=text[k];
 lastLines[row][k]=0;
 for(k=0;k<sizeof(rowBuffer);k++)rowBuffer[k]=0;
 while(*s && x<128){
   code=*s++;
   if(code>=0xE0 && code<0xF0 && s[0] && s[1]){code=((code&15)<<12)|((s[0]&63)<<6)|(s[1]&63);s+=2;}
   bitmap=0;w=8;
   if(code>=32 && code<127)bitmap=asciiFont[code-32];
   else {w=16;for(k=0;k<sizeof(hanFont)/sizeof(hanFont[0]);k++)if(hanFont[k].code==code){bitmap=hanFont[k].data;break;}}
   if(x+w>128)break;
   if(bitmap)for(j=0;j<2;j++)for(i=0;i<w;i++)rowBuffer[j*128+x+i]=bitmap[j*w+i];
   x+=w;
 }
 for(j=0;j<2;j++){
  first=128;last=0;
  for(k=0;k<128;k++)if(((volatile unsigned char *)screen)[(row*2+j)*128+k]!=rowBuffer[j*128+k]){
   if(first==128)first=k;
   last=k;
  }
  if(first<128){
   for(k=first;k<=last;k++)((volatile unsigned char *)screen)[(row*2+j)*128+k]=rowBuffer[j*128+k];
   WriteRange((unsigned char)(row*2+j),(unsigned char)first,(unsigned char)last,screen+(row*2+j)*128);
  }
 }
}
void LCD_SetCursor(uint8_t row,uint8_t column) { (void)column;cursorRow=row;textBuffer[0]=0; }
void LCD_WriteChar(char value) { unsigned int n=strlen(textBuffer);if(n<sizeof(textBuffer)-1){textBuffer[n]=value;textBuffer[n+1]=0;}LCD_PrintLine(cursorRow,textBuffer); }
void LCD_WriteString(const char *text) { LCD_PrintLine(cursorRow,text); }
void LCD_Printf(uint8_t row,const char *format,...){char b[64];va_list args;va_start(args,format);vsnprintf(b,sizeof(b),format,args);va_end(args);LCD_PrintLine(row,b);}
