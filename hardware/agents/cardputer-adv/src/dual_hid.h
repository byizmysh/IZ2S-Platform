#pragma once
#include <Arduino.h>
#include <USB.h>
#include <tusb.h>
#include <esp32-hal-tinyusb.h>
#include <freertos/semphr.h>
#include "switch_reports.h"

// Separate HID interfaces, matching the upstream PicoSwitch report formats.
// No Report IDs: interface 0 is a Switch controller, interface 1 a boot keyboard.
static constexpr uint8_t PAD_ITF=0, KBD_ITF=1;
#ifndef KEY_LEFT_SHIFT
#define KEY_LEFT_SHIFT 0x81
#endif
bool iz2s_hid_last_report_ok=true;
uint32_t iz2s_hid_reports_sent=0,iz2s_hid_report_errors=0;
static SemaphoreHandle_t reportDone[2]={nullptr,nullptr};
static uint8_t kbdReport[8]={0},padReport[8]={0,0,8,128,128,128,128,0};
static volatile uint8_t keyboardLeds=0;

extern "C" uint16_t iz2sDualDescriptor(uint8_t *dst,uint8_t *itf){
  const uint8_t descriptors[]={
    TUD_HID_INOUT_DESCRIPTOR(*itf,0,HID_ITF_PROTOCOL_NONE,sizeof(switch_usb_report_descriptor),0x02,0x81,64,1),
    TUD_HID_INOUT_DESCRIPTOR(uint8_t(*itf+1),0,HID_ITF_PROTOCOL_KEYBOARD,sizeof(keyboard_usb_report_descriptor),0x03,0x83,8,1)
  };
  *itf+=2;memcpy(dst,descriptors,sizeof(descriptors));return sizeof(descriptors);
}
extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance){
  return instance==PAD_ITF?switch_usb_report_descriptor:keyboard_usb_report_descriptor;
}
extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t length){
  if(id||instance>1)return 0;
  if(instance==KBD_ITF&&type==HID_REPORT_TYPE_OUTPUT){if(!length)return 0;buffer[0]=keyboardLeds;return 1;}
  if(type!=HID_REPORT_TYPE_INPUT)return 0;
  const uint16_t size=min(uint16_t(8),length);memcpy(buffer,instance==PAD_ITF?padReport:kbdReport,size);return size;
}
extern "C" void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t const *buffer,uint16_t length){
  if(instance==KBD_ITF&&length)keyboardLeds=buffer[0];
  // PicoSwitch echoes the wired controller's host output report.
  if(instance==PAD_ITF&&length<=64&&tud_hid_n_ready(PAD_ITF))tud_hid_n_report(PAD_ITF,id,buffer,length);
}
extern "C" void tud_hid_report_complete_cb(uint8_t instance,uint8_t const *,uint16_t){
  if(instance<2&&reportDone[instance])xSemaphoreGive(reportDone[instance]);
}
static bool sendHid(uint8_t instance,const uint8_t *report){
  bool ok=false;
  if(instance<2&&reportDone[instance]&&tud_mounted()&&!tud_suspended()){
    const uint32_t start=millis();
    while(!tud_hid_n_ready(instance)&&millis()-start<100)delay(1);
    xSemaphoreTake(reportDone[instance],0);
    if(tud_hid_n_ready(instance)&&tud_hid_n_report(instance,0,report,8))
      ok=xSemaphoreTake(reportDone[instance],pdMS_TO_TICKS(100))==pdTRUE;
  }
  iz2s_hid_last_report_ok=ok;if(ok)iz2s_hid_reports_sent++;else iz2s_hid_report_errors++;
  return ok;
}
static bool sendPad(uint16_t buttons=0,uint8_t hat=8){
  padReport[0]=buttons&255;padReport[1]=buttons>>8;padReport[2]=hat;
  return sendHid(PAD_ITF,padReport);
}
static bool padReady(){return tud_mounted()&&!tud_suspended()&&tud_hid_n_ready(PAD_ITF);}
static void pollGamepad(bool enabled=true){
  if(!enabled)return;
  static uint32_t sent=0;
  if(millis()-sent>=10&&padReady()){
    // Wired Switch controllers report neutral/held state continuously.
    if(tud_hid_n_report(PAD_ITF,0,padReport,8))sent=millis();
  }
}
class Iz2sKeyboard {
public:
  void begin(){
    for(auto &sem:reportDone)if(!sem)sem=xSemaphoreCreateBinary();
    tinyusb_enable_interface(USB_INTERFACE_HID,2*TUD_HID_INOUT_DESC_LEN,iz2sDualDescriptor);
  }
  void releaseAll(){memset(kbdReport,0,sizeof(kbdReport));sendHid(KBD_ITF,kbdReport);}
  void press(uint8_t key){
    if(key>=0x80&&key<0x88)kbdReport[0]|=1<<(key-0x80);
    else if(key>=0x88)kbdReport[2]=key-0x88;
    else {
      uint8_t code=0;bool shift=false;
      if(key>='a'&&key<='z')code=4+key-'a';
      else if(key>='A'&&key<='Z'){code=4+key-'A';shift=true;}
      else if(key>='1'&&key<='9')code=30+key-'1';
      else if(key=='0')code=39;
      else if(key==' ')code=44;
      else {
        const char *plain="-=[]\\;'`,./",*shifted="_+{}|:\"~<>?",*numbers="!@#$%^&*()";
        const uint8_t codes[]={45,46,47,48,49,51,52,53,54,55,56};
        for(int i=0;i<11;i++){if(key==plain[i])code=codes[i];if(key==shifted[i]){code=codes[i];shift=true;}}
        for(int i=0;i<10;i++)if(key==numbers[i]){code=30+i;shift=true;}
      }
      if(!code){iz2s_hid_last_report_ok=false;return;}
      if(shift)kbdReport[0]|=2;kbdReport[2]=code;
    }
    sendHid(KBD_ITF,kbdReport);
  }
};
