#include "settings-menu.h"
#include "brand.h"
#include <string.h>
#define WHITE 0xfff5f2ec
#define GREY 0xffb3a79b
static unsigned option_at(unsigned cat,unsigned row){for(unsigned i=0;i<PS1_OPTION_COUNT;i++)if(ps1_options[i].category==cat){if(!row--)return i;}return PS1_OPTION_COUNT;}
static unsigned rows(unsigned cat){unsigned n=0;for(unsigned i=0;i<PS1_OPTION_COUNT;i++)n+=ps1_options[i].category==cat;return n;}
void ps1_settings_menu_init(Ps1SettingsMenu*m,uint32_t b){memset(m,0,sizeof *m);m->previous=b;m->armed=!b;}
void ps1_settings_menu_step(Ps1SettingsMenu*m,Ps1Settings*s,uint32_t b){
 if(m->closed)return;
 if(m->pending_close){m->previous=b;if(!b)m->closed=1;return;}
 if(!m->armed){m->previous=b;if(!b)m->armed=1;return;}
 uint32_t press=b&~m->previous,dirs=b&(PS1_MENU_UP|PS1_MENU_DOWN|PS1_MENU_LEFT|PS1_MENU_RIGHT);
 if(dirs&&dirs==(m->previous&(PS1_MENU_UP|PS1_MENU_DOWN|PS1_MENU_LEFT|PS1_MENU_RIGHT))){m->held++;if(m->held>=20&&(m->held-20)%4==0)press|=dirs;}else m->held=0;m->previous=b;
 if(m->confirm_reset){if(press&PS1_MENU_A){if(m->category==PS1_CONTROLS)ps1_controls_reset(s,m->player);else ps1_settings_reset_category(s,m->category);m->confirm_reset=0;}else if(press&(PS1_MENU_B|PS1_PAUSE_MENU_BUTTON))m->confirm_reset=0;return;}
 if(m->confirm_disconnect){if(press&PS1_MENU_A){m->disconnect_request=m->player+1;m->confirm_disconnect=0;}else if(press&(PS1_MENU_B|PS1_PAUSE_MENU_BUTTON))m->confirm_disconnect=0;return;}
 if(press&(PS1_MENU_B|PS1_PAUSE_MENU_BUTTON)){m->pending_close=1;return;}
 if(press&(PS1_MENU_L|PS1_MENU_R)){m->category=(m->category+((press&PS1_MENU_R)?1:PS1_CATEGORIES-1))%PS1_CATEGORIES;m->row=0;m->held=0;return;}
 if(m->category==PS1_CONTROLS&&(press&(PS1_MENU_ZL|PS1_MENU_ZR))){m->player=(m->player+((press&PS1_MENU_ZL)?PS1_PLAYER_COUNT-1:1))%PS1_PLAYER_COUNT;m->action_error=m->disconnected=0;return;}
 unsigned control=m->category==PS1_CONTROLS,n=rows(m->category)+control*4;if(!n)return;
 if(press&PS1_MENU_UP)m->row=(m->row+n-1)%n;if(press&PS1_MENU_DOWN)m->row=(m->row+1)%n;
 if(control&&!m->row){if(press&(PS1_MENU_LEFT|PS1_MENU_RIGHT|PS1_MENU_A)){m->player=(m->player+((press&PS1_MENU_LEFT)?PS1_PLAYER_COUNT-1:1))%PS1_PLAYER_COUNT;m->action_error=m->disconnected=0;}if(press&PS1_MENU_Y)m->confirm_reset=1;return;}
 if(control&&m->row==1){if(press&PS1_MENU_LEFT)ps1_multitap_change(s,-1);else if(press&(PS1_MENU_RIGHT|PS1_MENU_A))ps1_multitap_change(s,1);if((press&PS1_MENU_X)&&s->prefs.multitap){s->prefs.multitap=0;s->dirty=1;}return;}
 if(control&&m->row==2){if(press&PS1_MENU_LEFT)ps1_controller_device_change(s,m->player,-1);else if(press&(PS1_MENU_RIGHT|PS1_MENU_A))ps1_controller_device_change(s,m->player,1);if((press&PS1_MENU_X)&&s->prefs.controller_device[m->player]){s->prefs.controller_device[m->player]=PS1_DEVICE_AUTO;s->dirty=1;}if(press&PS1_MENU_Y)m->confirm_reset=1;return;}
 if(control&&m->row==PS1_CONTROL_COUNT+3){if(press&PS1_MENU_A){m->confirm_disconnect=1;m->action_error=m->disconnected=0;}return;}
 unsigned id=option_at(m->category,m->row-control*3);
 if(press&PS1_MENU_LEFT){if(control)ps1_control_change(s,m->player,id,-1);else ps1_setting_change(s,id,-1);}
 else if(press&(PS1_MENU_RIGHT|PS1_MENU_A)){if(control)ps1_control_change(s,m->player,id,1);else ps1_setting_change(s,id,1);}
 if(press&PS1_MENU_X){if(control){while(ps1_control_value(&s->prefs,m->player,id)!=ps1_options[id].initial)ps1_control_change(s,m->player,id,1);}else if(s->prefs.value[id]!=ps1_options[id].initial){s->prefs.value[id]=ps1_options[id].initial;s->dirty=1;}}
 if(press&PS1_MENU_Y)m->confirm_reset=1;
}
void ps1_settings_menu_draw(const Ps1SettingsMenu*m,const Ps1Settings*s,const Ps1MenuConfig*c){
 uint32_t*p=c->rgba,accent=ps1_ui_accent(&s->prefs);ps1_ui_chrome(p,"SETTINGS - MAKE IT YOURS",&s->prefs);
 for(unsigned i=0;i<PS1_CATEGORIES;i++){unsigned x=24+i*100;if(i==m->category){ps1_ui_rect(p,x,86,94,25,0xff493623);ps1_ui_rect(p,x,109,94,2,accent);}ps1_ui_text(p,x+6,95,ps1_categories[i],1,i==m->category?WHITE:GREY,14);}
 if(m->category==PS1_ABOUT){
  ps1_ui_logo(p,36,138,2);ps1_ui_text(p,252,146,IZPSX_NAME,4,WHITE,8);ps1_ui_text(p,254,190,IZPSX_BYLINE,1,accent,24);
  ps1_ui_text(p,254,215,"Version " IZPSX_VERSION,1,GREY,32);
  ps1_ui_github(p,252,244,2,WHITE);ps1_ui_text(p,296,244,IZPSX_GITHUB_AUTHOR,1,WHITE,40);ps1_ui_text(p,296,264,IZPSX_GITHUB_URL,1,accent,44);
  ps1_ui_text(p,38,308,"PlayStation emulation. Your way.",1,WHITE,76);
  ps1_ui_text(p,38,331,"Two shared 128 KB memory cards.",1,GREY,76);
  ps1_ui_text(p,38,352,"Save in the game menu. Cards flush on pause and exit.",1,GREY,76);
  ps1_ui_text(p,38,373,c->launch_source?c->launch_source:"Loaded from SD card",1,accent,76);
  ps1_ui_text(p,38,394,IZPSX_PRODUCT_NAME " / GPL v2",1,GREY,76);

 }else if(m->category==PS1_CONTROLS){
  static const char*const players[]={"PLAYER 1","PLAYER 2","PLAYER 3","PLAYER 4"};
  for(unsigned player=0;player<PS1_PLAYER_COUNT;player++){
   unsigned x=24+player*150,selected=player==m->player;int connected=(c->controller_mask&(1u<<player))!=0;
   ps1_ui_rect(p,x,123,144,45,selected?0xff493623:0xff29221c);
   if(selected)ps1_ui_rect(p,x,165,144,3,accent);
   if(selected&&!m->row)ps1_ui_rect(p,x,123,3,45,accent);
   ps1_ui_text(p,x+10,131,players[player],1,selected?WHITE:GREY,16);
   const char*state=s->prefs.controller_device[player]==PS1_DEVICE_OFF?"DISABLED":player&&c->controller_error?"CONFIG ERROR":c->controller_status?(connected?"CONNECTED":"NOT CONNECTED"):"READY";
   ps1_ui_text(p,x+10,150,state,1,connected?accent:GREY,18);
  }
  const char*style=c->controller_style[m->player];
  ps1_ui_text(p,30,179,style?style:"ZL/ZR switches player. Select a player above to edit its profile.",1,GREY,84);
  for(unsigned i=0;i<PS1_CONTROL_COUNT+3;i++){
   unsigned y=201+i*25,selected=m->row==i+1;
   if(selected){ps1_ui_rect(p,22,y-5,596,24,0xff493623);ps1_ui_rect(p,22,y-5,3,24,accent);}
   const char*label=i==0?"Multitap (next game)":i==1?"Input device":i==PS1_CONTROL_COUNT+2?"Disconnect from Switch":ps1_options[PS1_SWAP_AB+i-2].label;
   const char*value=i==0?ps1_multitap_name(s->prefs.multitap):i==1?ps1_controller_device_name(s->prefs.controller_device[m->player]):i==PS1_CONTROL_COUNT+2?"A TO CONFIRM":ps1_options[PS1_SWAP_AB+i-2].values[ps1_control_value(&s->prefs,m->player,PS1_SWAP_AB+i-2)];
   ps1_ui_text(p,36,y,label,1,selected?WHITE:GREY,46);ps1_ui_text(p,438,y,value,1,selected?accent:GREY,25);
  }
  const char*help=!m->row?"Each player has a separate saved profile. ZL/ZR selects Player 1-4.":m->row==1?"Use Port 1 or Port 2 for games with Multitap support. Choose the game again.":m->row==2?"Off disconnects this game port. Select another device to replace it.":m->row==PS1_CONTROL_COUNT+3?"Disconnect the selected device from Switch. USB may reconnect it.":ps1_options[PS1_SWAP_AB+m->row-3].help;
  ps1_ui_text(p,30,378,help,1,GREY,84);
  ps1_ui_text(p,30,397,"Single Joy-Con: +/- Start. SL + SR + +/- opens the IZPSX menu.",1,accent,84);
 }else{
  unsigned n=rows(m->category);for(unsigned i=0;i<n;i++){
   unsigned id=option_at(m->category,i),y=130+i*31;int selected=i==m->row;
   if(selected){ps1_ui_rect(p,22,y-5,596,29,0xff493623);ps1_ui_rect(p,22,y-5,3,29,accent);}
   ps1_ui_text(p,36,y,ps1_options[id].label,1,selected?WHITE:GREY,46);
   ps1_ui_text(p,438,y,ps1_options[id].values[s->prefs.value[id]],1,selected?accent:GREY,28);
  }
  unsigned id=option_at(m->category,m->row);ps1_ui_text(p,30,372,ps1_options[id].help,1,GREY,95);
  ps1_ui_text(p,30,394,ps1_options[id].next_launch?"Next game launch - choose the game again to apply.":"Live setting - B saves and returns.",1,accent,95);
 }
 static const char*const resets[]={"Reset Player 1 controls? A confirms / B cancels.","Reset Player 2 controls? A confirms / B cancels.","Reset Player 3 controls? A confirms / B cancels.","Reset Player 4 controls? A confirms / B cancels."};
 static const char*const disconnects[]={"Disconnect Player 1 from Switch? A confirms / B cancels.","Disconnect Player 2 from Switch? A confirms / B cancels.","Disconnect Player 3 from Switch? A confirms / B cancels.","Disconnect Player 4 from Switch? A confirms / B cancels."};
 const char*reset=m->category==PS1_CONTROLS?resets[m->player]:"Reset this tab? A confirms / B cancels.";
 const char*disconnect=disconnects[m->player];
 const char*status=m->error?"Settings could not be saved. Press B to try again.":m->confirm_disconnect?disconnect:m->action_error?"Device could not disconnect. Choose a connected pad or use Off.":m->disconnected?"Disconnect requested. Pair a replacement in HOME, then select it.":m->confirm_reset?reset:s->warning?"An older settings copy was recovered. Check these values.":s->dirty?"B saves your changes and goes back.":s->generation?"Settings saved on SD.":"Default settings. Press B to save any changes.";
 ps1_ui_text(p,24,432,status,1,m->error?0xffb4b8ff:accent,95);
 ps1_ui_text(p,24,455,m->category==PS1_CONTROLS?(c->single_joycon?"SL/SR tabs  Outer shoulders player  Left/Right edit  X default  Y reset":"L/R tabs  ZL/ZR player  Left/Right edit  X default  Y reset"):"L/R tabs   Left/Right adjust   X default   Y reset tab",1,GREY,95);
}
