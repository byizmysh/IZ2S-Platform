void controllerHubOpen(){
 c.atlas[CB+1u]=3u;c.atlas[CH+CH_CURSOR]=controllerHubRow(controllerHubOrdinal(c.atlas[CH+CH_SOURCE]));
 c.atlas[CH+CH_TOUCH]=hbTouchCount()>0u?1u:0u;c.atlas[CH+CH_REPEAT]=0u;c.atlas[CH+CH_SCROLL]=0u;c.atlas[AM]=0u;c.atlas[AM+1u]=0u;c.transition.xy=vec2(0);hbEndFrame();
}
void controllerHubExit(bool held){c.atlas[CB+1u]=0u;c.library.w=held?1u:0u;hbEndFrame();}
void controllerHubChoose(uint row){
 if(!controllerHubReady()||c.atlas[CH+CH_REQUEST]!=c.atlas[CH+CH_ACK]||row==12u)return;
 c.atlas[CH+CH_REQUEST_SOURCE]=controllerHubIsSource(row)?row:c.atlas[CH+CH_SOURCE];
 c.atlas[CH+CH_REQUEST_PROFILE]=!controllerHubIsSource(row)?row-9u:clamp(c.atlas[CH+CH_PROFILE],1u,2u);
 memoryBarrierBuffer();uint sequence=c.atlas[CH+CH_REQUEST]+1u;c.atlas[CH+CH_REQUEST]=sequence==0u?1u:sequence;
}
void controllerHubInput(uint pressed){
 c.atlas[CH+CH_HEARTBEAT]++;
 bool held=hbTouchCount()>0u&&(c.touch[6]&2u)==0u,tap=held&&c.atlas[CH+CH_TOUCH]==0u;vec2 p=hbTouch(0u)*1.5;
 uint menu=c.atlas[AM],menuChoice=c.atlas[AM+1u];int action=uiMenuInput(pressed,held,tap,p,menu,menuChoice,2u);c.atlas[AM]=menu;c.atlas[AM+1u]=menuChoice;
 if(action==1){c.atlas[AM]=0u;controllerHubExit(held);return;}
 if(menu!=0u||action>=0||(pressed&HB_MINUS)!=0u||(tap&&p.x<220.0&&p.y<130.0)){c.atlas[CH+CH_TOUCH]=held?1u:0u;hbEndFrame();return;}
 if((pressed&HB_B)!=0u){controllerHubExit(held);return;}
 uint ordinal=controllerHubOrdinal(c.atlas[CH+CH_CURSOR]),count=controllerHubRowCount();int move=(pressed&HB_UP)!=0u?-1:(pressed&HB_DOWN)!=0u?1:0;
 if(c.atlas[CH+CH_REPEAT]>0u)c.atlas[CH+CH_REPEAT]--;
 float stick=hbStick(false).y;if(hbHas(HB_PAD_SOURCES))for(uint i=0u;i<5u;i++)if((hbWord(640u+i*64u+32u)&1u)!=0u){float y=clamp(float(int(hbWord(640u+i*64u+20u)))/32767.0,-1.0,1.0);if(abs(y)>abs(stick))stick=y;}
 if(abs(stick)>.55&&c.atlas[CH+CH_REPEAT]==0u){move=stick>0.0?-1:1;c.atlas[CH+CH_REPEAT]=12u;}
 if(move!=0)ordinal=uint((int(ordinal)+move+int(count))%int(count));bool choose=(pressed&HB_A)!=0u;
 if(tap&&p.x>=750.0&&p.x<=1850.0)for(uint i=c.atlas[CH+CH_SCROLL];i<min(count,c.atlas[CH+CH_SCROLL]+7u);i++)if(abs(p.y-controllerHubRowY(i))<34.0){ordinal=i;choose=true;}
 uint scroll=min(c.atlas[CH+CH_SCROLL],count>7u?count-7u:0u);if(ordinal<scroll)scroll=ordinal;else if(ordinal>=scroll+7u)scroll=ordinal-6u;c.atlas[CH+CH_SCROLL]=scroll;
 uint row=controllerHubRow(ordinal);c.atlas[CH+CH_CURSOR]=row;c.atlas[CH+CH_TOUCH]=held?1u:0u;
 if(choose){if(row==12u){controllerHubExit(held);return;}controllerHubChoose(row);}hbEndFrame();
}
