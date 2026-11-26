#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "oled_font.h"
static unsigned char screen[1024];
static volatile unsigned char rowBuffer[256];
static char lastLines[4][64];
static volatile unsigned char backBuffer[1024];
static unsigned char dirtyStart[8],dirtyEnd[8],frameActive,hideDuringFlush;
static unsigned char displayOn=1;
static unsigned commandCount;
static void Command(unsigned char value){assert(value==0xAE || value==0xAF);displayOn=(value==0xAF);++commandCount;}

static unsigned char ram[1024];
static unsigned count;
static void WriteRange(unsigned char page,unsigned char first,unsigned char last,const unsigned char *data){unsigned x;if(frameActive && hideDuringFlush)assert(displayOn==0);for(x=first;x<=last;x++){ram[page*128+x]=data[x];++count;}}
/* CURRENT_LCD_PRINT_LINE */
int main(void){
 unsigned before;unsigned char expected[1024];
 LCD_PrintLine(0,"RPM 450");before=count;
 LCD_PrintLine(0,"RPM 450");assert(count==before);puts("unchanged text: zero transmitted bytes PASS");
 LCD_PrintLine(0,"RPM 451");printf("one digit update: %u pixel bytes (old driver: 256)\n",count-before);assert(count-before<32);
 LCD_PrintLine(0,"R");memcpy(expected,ram,1024);
 memset(screen,0,1024);memset(ram,0,1024);memset(lastLines,0,sizeof(lastLines));LCD_PrintLine(0,"R");assert(memcmp(expected,ram,1024)==0);puts("shorter text clears old pixels PASS");
 LCD_PrintLine(1,"ABCDEFGH");LCD_PrintLine(1,"1");memcpy(expected,ram,1024);
 memset(screen,0,1024);memset(ram,0,1024);memset(lastLines,0,sizeof(lastLines));LCD_PrintLine(0,"R");LCD_PrintLine(1,"1");assert(memcmp(expected,ram,1024)==0);puts("partial update matches fresh rendering PASS");
 before=count;
 LCD_BeginFrame(1);
 LCD_PrintLine(0,"NEW PAGE");LCD_PrintLine(1,"SECOND ROW");
 assert(count==before && displayOn==1 && commandCount==0);
 LCD_EndFrame();assert(count>before && displayOn==1 && commandCount==2);
 puts("page composed before transmission; display restored after flush PASS");
 before=commandCount;LCD_BeginFrame(0);LCD_PrintLine(0,"NEW PAGF");LCD_EndFrame();
 assert(displayOn==1 && commandCount==before);puts("numeric/local update keeps display on PASS");
 return 0;
}
