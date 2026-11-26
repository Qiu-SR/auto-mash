from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[1]/'Firmware'/'Application'
chars=(r/'oled-chars.txt').read_text(encoding='utf-8')
f=ImageFont.truetype(r'C:\Windows\Fonts\simsun.ttc',16);a=ImageFont.truetype(r'C:\Windows\Fonts\consola.ttf',14)
def glyph(c,w,font):
 im=Image.new('1',(w,16));ImageDraw.Draw(im).text((0,0),c,font=font,fill=1,anchor='lt')
 return [sum((1<<y) for y in range(8) if im.getpixel((x,y+page*8))) for page in range(2) for x in range(w)]
out='/* 中文点阵为 16×16，英数点阵为 8×16；按控制器页组织字节。 */\n'
out+='static const unsigned char asciiFont[95][16]={\n'+',\n'.join('{'+','.join(map(str,glyph(chr(c),8,a)))+'}' for c in range(32,127))+'};\n'
out+='typedef struct { unsigned short code; unsigned char data[32]; } HanGlyph;\nstatic const HanGlyph hanFont[]={\n'+',\n'.join('{'+str(ord(c))+',{'+','.join(map(str,glyph(c,16,f)))+'}}' for c in chars)+'};\n'
(r/'oled_font.h').write_text(out,encoding='utf-8-sig');print('Glyphs:',len(chars))
