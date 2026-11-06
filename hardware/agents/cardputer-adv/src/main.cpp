// IZ2S Cardputer ADV: authenticated Wi-Fi plans -> USB HID, without command replay.
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <USB.h>
#undef KEY_BACKSPACE
#undef KEY_TAB
#include <USBHIDKeyboard.h>
#include <tusb.h>
#include <deque>
#include <mbedtls/sha256.h>
#include "screen.h"
#include "usb_recovery.h"

// Results from the pinned boot-keyboard compatibility patch.
extern bool iz2s_hid_last_report_ok;
extern uint32_t iz2s_hid_reports_sent,iz2s_hid_report_errors;

static const char *VERSION="0.21.3", *CONFIG="/IZ2S/input.json";
static constexpr uint32_t KEY_MS=60, HEARTBEAT_MS=15000, MAX_JOB_MS=7200000;
WebServer server(80);
USBHIDKeyboard keyboard;
SPIClass sdBus(HSPI);
Preferences journal;
String wifiName,wifiPassword,apiToken,bootId,contentId;
bool sdReady=false,configured=false;
uint32_t lastDraw=0,nextReconnect=0;
bool wifiSetupHeld=false,wifiMenuOffered=false;
uint32_t wifiStarted=0;
DynamicJsonDocument activePlan(49152);
struct Op { uint8_t type; String text; uint8_t key; bool shift; uint32_t wait; };
std::deque<Op> ops;
String jobId,jobHash,jobState="idle",jobError,source;
size_t stepIndex=0,sourcePosition=0,textPosition=0;
uint32_t sourceLine=0,totalLines=0,sourceAfter=0,lastContact=0,started=0,deadline=0;
bool sourceDone=true,down=false,releasing=false,capsOn=false;
uint8_t seenSlot=0;
volatile uint32_t usbGeneration=0;
volatile bool usbAwaitingMount=false;
UsbRecoveryPolicy usbRecovery;
bool panelSeen=false;
uint32_t panelSeenAt=0;

bool usbReady(){return !usbRecovery.detached()&&!usbAwaitingMount&&tud_mounted()&&!tud_suspended();}
String usbState(){return usbRecovery.detached()?"reconnecting":tud_mounted()&&!usbAwaitingMount?(tud_suspended()?"suspended":"configured"):tud_connected()?"attached":"not attached";}
void usbEvent(void *,esp_event_base_t eventBase,int32_t eventId,void *){
  if(eventBase!=ARDUINO_USB_EVENTS)return;
  if(eventId==ARDUINO_USB_STARTED_EVENT){usbAwaitingMount=false;usbGeneration++;}
  else if(eventId==ARDUINO_USB_STOPPED_EVENT){usbAwaitingMount=true;usbGeneration++;}
}
bool running(){return jobState=="running";}
void recoverUsb(){
  const uint32_t now=millis();
  const bool observed=panelSeen&&uint32_t(now-panelSeenAt)<HEARTBEAT_MS&&WiFi.status()==WL_CONNECTED;
  const auto action=usbRecovery.update(now,tud_mounted()&&!usbAwaitingMount,running(),observed);
  if(action==UsbRecoveryPolicy::Detach){usbAwaitingMount=true;usbGeneration++;tud_disconnect();}
  else if(action==UsbRecoveryPolicy::Attach)tud_connect();
}
bool due(uint32_t time){return int32_t(millis()-time)>=0;}
void releaseKeys(){keyboard.releaseAll();down=false;releasing=false;}
void finish(const String &state,const String &error=""){
  releaseKeys();ops.clear();jobState=state;jobError=error;activePlan.clear();source="";
}
void reply(int code,JsonDocument &doc){String body;serializeJson(doc,body);server.sendHeader("Cache-Control","no-store");server.send(code,"application/json",body);}
void errorReply(int code,const String &error){StaticJsonDocument<256> d;d["error"]=error;reply(code,d);}
bool auth(){
  String expected="Bearer "+apiToken,value=server.header("Authorization");
  uint8_t difference=uint8_t(value.length()!=expected.length());
  for(size_t i=0;i<expected.length();i++)difference|=uint8_t(expected[i]^(i<value.length()?value[i]:0));
  if(!configured||difference){errorReply(401,"Pair this device with the IZ2S panel");return false;}return true;
}
void jobReply(int code=200){
  StaticJsonDocument<768> d;d["id"]=jobId;d["bootId"]=bootId;d["status"]=jobState;
  d["line"]=sourceLine;d["total"]=totalLines;d["step"]=stepIndex;d["error"]=jobError;
  reply(code,d);
}
bool ascii(const String &s,bool lines=false){for(size_t i=0;i<s.length();i++){uint8_t c=s[i];if(c<32||c>126){if(!(lines&&(c=='\r'||c=='\n'||c=='\t')))return false;}}return true;}
bool identifier(const String &id){if(id.length()!=36)return false;for(size_t i=0;i<id.length();i++)if(!isxdigit(id[i])&&id[i]!='-')return false;return true;}
uint8_t special(const String &key){
  if(key=="enter")return 40;if(key=="escape")return 41;if(key=="backspace")return 42;if(key=="tab")return 43;
  if(key=="capslock")return 57;if(key=="f4")return 61;if(key=="f7")return 64;if(key=="f8")return 65;
  if(key=="home")return 74;if(key=="delete")return 76;if(key=="end")return 77;
  if(key=="right")return 79;if(key=="left")return 80;if(key=="down")return 81;if(key=="up")return 82;return 0;
}
void keyOp(uint8_t key,bool shift=false){ops.push_back({1,"",key,shift,0});}
void textOp(const String &text){if(text.length())ops.push_back({2,text,0,false,0});}
void waitOp(uint32_t ms){if(ms)ops.push_back({3,"",0,false,ms});}
void lineOp(const String &text,uint32_t delay){textOp(text);keyOp(41);waitOp(200);keyOp(40);waitOp(delay);}
void fillOps(){
  JsonArray steps=activePlan["steps"].as<JsonArray>();
  if(!sourceDone&&stepIndex==sourceAfter){
    if(sourcePosition>=source.length()){sourceDone=true;return;}
    size_t end=source.indexOf('\n',sourcePosition);if(end==size_t(-1))end=source.length();
    String line=source.substring(sourcePosition,end);sourcePosition=end+1;line.replace("\r","");line.replace("\t","  ");
    keyOp(74,true);keyOp(76);textOp(line);
    String trim=line;trim.trim();if(trim.length()){keyOp(41);waitOp(200);}keyOp(40);waitOp(300);
    trim.toUpperCase();if(trim=="END")waitOp(800);
    ops.push_back({4,"",0,false,0});return;
  }
  if(stepIndex>=steps.size()){finish("complete");return;}
  JsonObject step=steps[stepIndex++];String type=step["type"]|"";
  if(type=="line")lineOp(step["text"].as<String>(),step["delayMs"]|500);
  else if(type=="key"){keyOp(special(step["key"].as<String>()));waitOp(step["delayMs"]|500);}
  else waitOp(step["ms"]|0);
}
void tick(){
  if(!running())return;
  if(!usbReady()){finish("failed","USB keyboard disconnected; commands were not replayed");return;}
  if(WiFi.status()!=WL_CONNECTED||millis()-lastContact>HEARTBEAT_MS){finish("failed","Server contact lost; input stopped");return;}
  if(millis()-started>MAX_JOB_MS){finish("failed","Input job deadline exceeded");return;}
  if(!due(deadline))return;
  if(ops.empty()){textPosition=0;fillOps();return;}
  Op &op=ops.front();
  if(op.type==3){deadline=millis()+op.wait;ops.pop_front();return;}
  if(op.type==4){sourceLine++;ops.pop_front();return;}
  if(releasing){releasing=false;if(op.type==2&&++textPosition<op.text.length())return;ops.pop_front();textPosition=0;return;}
  if(down){keyboard.releaseAll();if(!iz2s_hid_last_report_ok){finish("failed","USB host did not accept key release; input stopped");return;}down=false;releasing=true;deadline=millis()+KEY_MS;return;}
  if(op.type==2){char c=op.text[textPosition];if(capsOn&&isalpha(c))c=islower(c)?toupper(c):tolower(c);keyboard.press(uint8_t(c));}
  else {if(op.shift){keyboard.press(KEY_LEFT_SHIFT);if(!iz2s_hid_last_report_ok){finish("failed","USB host did not accept Shift; input stopped");return;}}keyboard.press(uint8_t(op.key+0x88));}
  if(!iz2s_hid_last_report_ok){finish("failed","USB host did not accept the keyboard report; input stopped");return;}
  down=true;deadline=millis()+KEY_MS;
}
bool digestId(const String &id){if(id.length()!=64)return false;for(size_t i=0;i<id.length();i++)if(!isxdigit(id[i]))return false;return true;}
void stageContent(const String &id){
  if(!auth())return;
  if(running()){errorReply(409,"Stop input before uploading a new text package");return;}
  if(!sdReady){errorReply(503,"Insert the paired SD card before uploading input text");return;}
  String body=server.arg("plain");
  if(!digestId(id)||body.length()>32768){errorReply(413,"Invalid or oversized text package");return;}
  uint8_t sum[32];char hex[65];mbedtls_sha256_ret((const uint8_t*)body.c_str(),body.length(),sum,0);
  for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",sum[i]);
  if(id!=String(hex)){errorReply(400,"Text package checksum differs");return;}
  activePlan.clear();
  if(deserializeJson(activePlan,body)||!activePlan.is<JsonObject>()||activePlan.size()>16){activePlan.clear();errorReply(400,"Invalid text package");return;}
  for(JsonPair item:activePlan.as<JsonObject>())if(!item.value().is<JsonObject>()){activePlan.clear();errorReply(400,"Invalid input action");return;}
  activePlan.clear();SD.mkdir("/IZ2S");File file=SD.open("/IZ2S/content.tmp",FILE_WRITE);
  if(!file){errorReply(503,"Cannot store input text on SD");return;}
  size_t written=file.print(body);file.flush();file.close();
  if(written!=body.length()){SD.remove("/IZ2S/content.tmp");errorReply(503,"Incomplete SD text write");return;}
  contentId="";SD.remove("/IZ2S/content.json");
  if(!SD.rename("/IZ2S/content.tmp","/IZ2S/content.json")){errorReply(503,"Cannot commit SD text package");return;}
  contentId=id;StaticJsonDocument<128> result;result["contentId"]=contentId;result["status"]="staged";reply(200,result);
}
void acceptJob(){
  if(!auth())return;
  if(server.arg("plain").length()>32768){errorReply(413,"Input plan exceeds 32 KiB");return;}
  DynamicJsonDocument incoming(49152);
  if(deserializeJson(incoming,server.arg("plain"))){errorReply(400,"Invalid input plan");return;}
  String id=incoming["requestId"]|"",hash=incoming["digest"]|"";
  if(!identifier(id)||!digestId(hash)||incoming["bootId"].as<String>()!=bootId){errorReply(409,"Device changed; obtain fresh status before input");return;}
  if(id==jobId){if(hash!=jobHash){errorReply(409,"Job identity reused with different input");return;}lastContact=millis();jobReply();return;}
  if(running()){errorReply(409,"Input device is busy");return;}
  if(!usbReady()){errorReply(409,"Connect the USB data cable to Switch");return;}
  for(int i=0;i<8;i++)if(journal.getString(("seen"+String(i)).c_str())==id){errorReply(409,"Job was already accepted before; input was not replayed");return;}
  String requestedContent=incoming["contentId"]|"",planKey=incoming["planKey"]|"";
  if(!contentId.length()||requestedContent!=contentId||!planKey.length()){errorReply(409,"Upload all input text before requesting an action");return;}
  File staged=SD.open("/IZ2S/content.json");activePlan.clear();
  if(!staged||deserializeJson(activePlan,staged)){if(staged)staged.close();activePlan.clear();errorReply(503,"Staged text is unavailable; upload it again");return;}
  staged.close();
  JsonObject selected=activePlan[planKey].as<JsonObject>();
  if(selected.isNull()){activePlan.clear();errorReply(404,"Input action is not in the staged text package");return;}
  incoming.clear();incoming.set(selected);activePlan.clear();
  JsonArray steps=incoming["steps"].as<JsonArray>();
  if(steps.isNull()||steps.size()>64){errorReply(400,"Expected at most 64 input steps");return;}
  for(JsonObject step:steps){String type=step["type"]|"";uint32_t ms=type=="sleep"?(step["ms"]|0):(step["delayMs"]|500);
    if(ms>120000||(type!="line"&&type!="key"&&type!="sleep")||(type=="line"&&(!step["text"].is<const char*>()||!ascii(step["text"].as<String>())||step["text"].as<String>().length()>2048))||(type=="key"&&!special(step["key"].as<String>()))){errorReply(400,"Unsupported input step");return;}
  }
  String text=incoming["source"]|"";int after=incoming["sourceAfter"]|int(steps.size());
  if(!ascii(text,true)||text.length()>16384||after<0||after>int(steps.size())){errorReply(400,"Invalid bootstrap source");return;}
  // One durable receipt per job. A reboot can never silently repeat accepted input.
  if(!journal.putString(("seen"+String(seenSlot)).c_str(),id)){errorReply(503,"Input receipt could not be saved");return;}
  seenSlot=(seenSlot+1)%8;journal.putUChar("slot",seenSlot);
  activePlan.clear();activePlan.set(incoming);source=text;source.replace("\r\n","\n");source.replace("\r","\n");
  jobId=id;jobHash=hash;jobState="running";jobError="";sourceAfter=after;sourceDone=source.isEmpty();
  sourcePosition=0;sourceLine=0;totalLines=0;for(size_t i=0;i<source.length();i++)if(source[i]=='\n')totalLines++;
  if(source.length()&&source[source.length()-1]!='\n')totalLines++;
  capsOn=incoming["capsLock"]|false;stepIndex=0;ops.clear();textPosition=0;releaseKeys();
  started=lastContact=millis();deadline=millis();jobReply(202);
}
void setupHttp(){
  const char *headers[]={"Authorization"};server.collectHeaders(headers,1);
  server.on("/v1/status",HTTP_GET,[]{if(!auth())return;panelSeen=true;panelSeenAt=millis();StaticJsonDocument<1792> d;
    d["device"]="iz2s-cardputer-adv";d["apiVersion"]=2;d["contentId"]=contentId;d["version"]=VERSION;d["bootId"]=bootId;d["keyboard"]=usbReady();d["busy"]=running();
    d["usbKeyboardFormat"]="boot-8-byte";d["hidProtocol"]=tud_hid_n_get_protocol(0);d["reportsSent"]=iz2s_hid_reports_sent;d["reportErrors"]=iz2s_hid_report_errors;
    d["usbState"]=usbState();d["usbConnected"]=tud_connected()||tud_mounted();d["usbConfigured"]=tud_mounted()&&!usbAwaitingMount&&!usbRecovery.detached();d["usbSuspended"]=tud_mounted()&&tud_suspended();
    d["usbHidReady"]=tud_hid_n_ready(0);d["usbGeneration"]=usbGeneration;d["usbSerial"]=USB.serialNumber();
    d["usbRecovering"]=usbRecovery.detached();d["usbReconnects"]=usbRecovery.attempts();
    d["ip"]=WiFi.localIP().toString();d["jobId"]=jobId;reply(200,d);});
  server.on("/v1/jobs",HTTP_POST,acceptJob);
  server.on("/v1/stop",HTTP_POST,[]{if(!auth())return;StaticJsonDocument<256> d;
    if(deserializeJson(d,server.arg("plain"))){errorReply(400,"Invalid cancellation");return;}
    String id=d["id"]|"";
    if(id.length()&&jobId.length()&&id!=jobId){errorReply(409,"A different input job owns the keyboard");return;}
    if(running())finish("cancelled","Stopped by the panel");else releaseKeys();jobReply();});
  server.onNotFound([]{String content="/v1/content/";if(server.method()==HTTP_PUT&&server.uri().startsWith(content)){stageContent(server.uri().substring(content.length()));return;}if(!auth())return;String prefix="/v1/jobs/";
    if(server.method()==HTTP_GET&&server.uri().startsWith(prefix)&&server.uri().substring(prefix.length())==jobId){lastContact=millis();jobReply();return;}
    errorReply(404,"Input job not found");});
  server.begin();MDNS.begin("iz2s-adv");MDNS.addService("http","tcp",80);
}
void saveConfiguration(){
  if(!sdReady)return;SD.mkdir("/IZ2S");File f=SD.open(CONFIG,FILE_WRITE);if(!f)return;
  StaticJsonDocument<768> d;d["ssid"]=wifiName;d["password"]=wifiPassword;d["token"]=apiToken;serializeJson(d,f);f.close();
}
String prompt(const char *title,bool secret=false,const String &context=""){
  String value;bool reveal=false;
  Screen::input(title,value,secret,context,reveal);
  while(true){M5Cardputer.update();
    if(M5Cardputer.Keyboard.isChange()&&M5Cardputer.Keyboard.isPressed()){
      auto keys=M5Cardputer.Keyboard.keysState();if(keys.del&&value.length())value.remove(value.length()-1);
      if(secret&&keys.tab)reveal=!reveal;
      for(char c:keys.word)if(c>=32&&c<=126&&value.length()<(secret?63:32))value+=c;
      if(keys.enter&&(value.length()||secret))return value;
      Screen::input(title,value,secret,context,reveal);
    }
    delay(10);
  }
}
void configureWifi(bool retry=false){
  releaseKeys();WiFi.disconnect();WiFi.scanDelete();
  Screen::message("Scanning Wi-Fi","Looking for nearby networks...");
  int count=WiFi.scanNetworks(false,false),page=0;String chosen;bool manual=false;
  while(!chosen.length()&&!manual){
    Screen::networks(count,page,retry);
    bool redraw=false;
    while(!redraw){M5Cardputer.update();if(M5Cardputer.Keyboard.isChange()&&M5Cardputer.Keyboard.isPressed()){
      auto keys=M5Cardputer.Keyboard.keysState();
      for(char raw:keys.word){char c=tolower(raw);
        if(c>='1'&&c<='5'&&page*5+c-'1'<count){chosen=WiFi.SSID(page*5+c-'1');redraw=true;}
        if(c=='n'&&page*5+5<count){page++;redraw=true;}
        if(c=='p'&&page>0){page--;redraw=true;}
        if(c=='m'){manual=true;redraw=true;}
        if(c=='r'){Screen::message("Scanning Wi-Fi","Looking for nearby networks...");WiFi.scanDelete();count=WiFi.scanNetworks(false,false);page=0;redraw=true;}
      }
    }delay(10);}
  }
  bool open=false;if(!manual)for(int i=0;i<count;i++)if(WiFi.SSID(i)==chosen)open=WiFi.encryptionType(i)==WIFI_AUTH_OPEN;
  WiFi.scanDelete();wifiName=manual?prompt("Network name"):chosen;
  wifiPassword=open?"":prompt("Wi-Fi password",true,wifiName);saveConfiguration();
  Screen::message("Connecting...","Joining your local Wi-Fi network");
  WiFi.begin(wifiName.c_str(),wifiPassword.c_str());wifiStarted=millis();wifiMenuOffered=false;
}

void setup(){
  auto cfg=M5.config();M5Cardputer.begin(cfg,true);M5Cardputer.Display.setRotation(1);Screen::init();
  Screen::message("Starting...","Preparing Wi-Fi and USB keyboard");
  sdBus.begin(40,39,14,12);sdReady=SD.begin(12,sdBus,20000000);
  if(sdReady){File f=SD.open(CONFIG);if(f){StaticJsonDocument<1024> d;if(!deserializeJson(d,f)){wifiName=d["ssid"]|"";wifiPassword=d["password"]|"";apiToken=d["token"]|"";}f.close();}}
  if(apiToken.length()!=64){char text[65];for(int i=0;i<8;i++)snprintf(text+i*8,9,"%08x",esp_random());apiToken=text;saveConfiguration();}
  char id[33];for(int i=0;i<4;i++)snprintf(id+i*8,9,"%08x",esp_random());bootId=id;
  journal.begin("iz2s-adv",false);seenSlot=journal.getUChar("slot",0)%8;
  WiFi.mode(WIFI_STA);WiFi.setSleep(false);
  if(wifiName.isEmpty())configureWifi();
  else {WiFi.begin(wifiName.c_str(),wifiPassword.c_str());wifiStarted=millis();}
  USB.onEvent(usbEvent);keyboard.begin();USB.productName("IZ2S Cardputer ADV Keyboard");USB.manufacturerName("IZMYSH");USB.begin();
  configured=true;setupHttp();
}
void loop(){
  M5Cardputer.update();server.handleClient();recoverUsb();tick();
  if(M5Cardputer.BtnA.wasPressed()&&running())finish("cancelled","Stopped on the device");
  if(!M5Cardputer.BtnA.isPressed())wifiSetupHeld=false;
  if(!running()&&!wifiSetupHeld&&M5Cardputer.BtnA.pressedFor(2000)){wifiSetupHeld=true;configureWifi();}
  if(WiFi.status()==WL_CONNECTED){wifiStarted=millis();wifiMenuOffered=false;}
  else if(!running()&&!wifiMenuOffered&&millis()-wifiStarted>20000){wifiMenuOffered=true;configureWifi(true);}
  if(WiFi.status()!=WL_CONNECTED&&due(nextReconnect)){nextReconnect=millis()+10000;WiFi.reconnect();}
  if(millis()-lastDraw>500){lastDraw=millis();
    Screen::status(WiFi.status()==WL_CONNECTED,WiFi.localIP().toString(),usbReady(),usbState(),running(),sdReady,jobState,sourceLine,totalLines,jobError);
  }
  delay(1);
}
