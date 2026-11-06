#pragma once

// One 240 x 135 frame, shared by every device screen. Integer font sizes keep
// small text sharp; the sprite prevents a black flash between redraws.
namespace Screen {
static constexpr uint16_t BG=0x0862, PANEL=0x10e4, EDGE=0x2947,
  TEXT=0xef7d, MUTED=0x94b4, LIME=0xcfa3, CYAN=0x5e9f, RED=0xfaa9;
M5Canvas frame(&M5Cardputer.Display);
lgfx::LGFXBase &canvas(){return frame.width()==240?static_cast<lgfx::LGFXBase&>(frame):static_cast<lgfx::LGFXBase&>(M5Cardputer.Display);}
void init(){frame.setColorDepth(8);frame.createSprite(240,135);}
void font(bool large=false){if(large)canvas().setFont(&fonts::Font2);else canvas().setFont(&fonts::Font0);canvas().setTextSize(1);canvas().setTextWrap(false);}
String fit(String value,int width,bool large=false){
  font(large);if(canvas().textWidth(value)<=width)return value;
  while(value.length()&&canvas().textWidth(value+"...")>width){
    int at=value.length()-1;while(at>0&&(uint8_t(value[at])&0xc0)==0x80)at--;value.remove(at);
  }return value+"...";
}
void text(int x,int y,const String &value,uint16_t color=TEXT,bool large=false){
  font(large);canvas().setTextColor(color);canvas().setCursor(x,y);canvas().print(value);
}
void begin(const char *section){
  auto &c=canvas();c.fillScreen(BG);c.fillRect(0,0,240,21,PANEL);c.fillRect(0,20,240,1,EDGE);
  c.fillRoundRect(6,5,4,11,2,LIME);text(15,2,"IZ2S",TEXT,true);
  text(60,7,section,MUTED);text(174,7,"ADV / 0.21",LIME);
}
void end(const char *help,const char *second=""){
  auto &c=canvas();c.fillRect(0,109,240,26,PANEL);c.drawFastHLine(0,109,240,EDGE);
  text(7,second[0]?114:120,help,MUTED);if(second[0])text(7,125,second,MUTED);
  if(frame.width()==240)frame.pushSprite(0,0);
}
void message(const char *title,const char *detail){
  begin("SETUP");canvas().drawCircle(20,44,7,CYAN);canvas().fillCircle(20,44,2,LIME);
  text(35,35,title,TEXT,true);text(10,69,fit(detail,220),MUTED);end("IZ2S  /  WI-FI -> USB INPUT");
}
void input(const char *title,const String &value,bool secret,const String &context,bool reveal=false){
  begin("WI-FI");text(9,26,title,TEXT,true);text(10,46,fit(context,220),MUTED);
  canvas().fillRoundRect(8,62,224,31,5,PANEL);canvas().drawRoundRect(8,62,224,31,5,CYAN);
  String shown=value;if(secret&&!reveal){shown="";for(size_t i=0;i<value.length();i++)shown+='*';}
  auto &c=canvas();c.drawRoundRect(14,73,15,10,2,MUTED);
  for(int row=0;row<2;row++)for(int col=0;col<4;col++)c.drawPixel(17+col*3,76+row*3,MUTED);
  font(true);while(shown.length()&&c.textWidth(shown)>(secret?158:185))shown.remove(0,1);
  text(36,69,shown,TEXT,true);c.fillRect(38+c.textWidth(shown),72,2,13,LIME);
  if(secret){c.drawEllipse(215,77,8,4,reveal?LIME:MUTED);c.fillCircle(215,77,2,reveal?LIME:MUTED);if(!reveal)c.drawLine(208,83,222,70,MUTED);}
  text(10,98,"Type on the ADV keyboard",MUTED);
  if(secret)end("ENTER CONNECT   DEL ERASE",reveal?"TAB HIDE PASSWORD":"TAB SHOW PASSWORD");
  else end("DEL ERASE    ENTER CONTINUE");
}
void networks(int count,int page,bool retry){
  begin("WI-FI");text(9,25,retry?"RETRY WI-FI":"CHOOSE WI-FI",retry?RED:CYAN);
  text(207,25,String(page+1)+"/"+String(max(1,(count+4)/5)),MUTED);
  if(count<=0){text(12,52,"No networks found",TEXT,true);text(12,79,"Try scanning again",MUTED);}
  for(int row=0;row<5&&page*5+row<count;row++){
    int y=33+row*15,index=page*5+row;auto &c=canvas();
    c.fillRoundRect(7,y,226,15,3,PANEL);c.fillRoundRect(9,y+1,17,13,2,EDGE);
    text(14,y+3,String(row+1),LIME);text(32,y-1,fit(WiFi.SSID(index),175,true),TEXT,true);
    if(WiFi.encryptionType(index)!=WIFI_AUTH_OPEN){c.drawRoundRect(217,y+6,7,6,1,MUTED);c.drawCircle(220,y+5,2,MUTED);}
  }end("1-5 SELECT    N/P PAGE","R RESCAN      M MANUAL");
}
void status(bool wifi,const String &ip,bool usb,const String &usbState,bool busy,bool sd,const String &job,
            uint32_t line,uint32_t total,const String &error){
  begin(busy?"TYPING":"INPUT");auto &c=canvas();
  c.fillRoundRect(7,26,226,33,5,PANEL);text(14,31,"WI-FI",MUTED);
  c.fillCircle(220,34,3,wifi?LIME:RED);text(14,41,wifi?ip:"Connecting...",wifi?CYAN:TEXT,true);
  c.fillRoundRect(7,64,226,22,4,PANEL);text(14,71,"USB",MUTED);
  const char *usbLabel=usb?"Keyboard ready":usbState=="reconnecting"?"Reconnecting USB":usbState=="suspended"?"USB inactive":usbState=="attached"?"Configuring USB":"Waiting USB host";
  text(51,67,usbLabel,usb?LIME:TEXT,true);
  String detail=!sd?"Insert the paired SD card":error.length()?error:busy?String("Typing ")+line+" / "+total:job=="complete"?"Input complete":job=="cancelled"?"Input stopped":"Waiting for panel";
  text(10,96,fit(detail,220),!sd||error.length()?RED:busy?CYAN:MUTED);
  if(busy&&total){c.fillRoundRect(9,106,222,2,1,EDGE);c.fillRoundRect(9,106,max(2,int(min(line,total)*222/total)),2,1,LIME);}
  end(busy?"G0 STOP INPUT":"G0 HOLD 2s: WI-FI SETUP");
}
} // namespace Screen
