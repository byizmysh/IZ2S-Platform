const uint AM=(0x701800u-1024u)/4u;
int uiMenuInput(uint pressed,bool held,bool tap,vec2 p,inout uint menu,inout uint selected,uint count){
 bool badge=tap&&all(lessThanEqual(abs(p-vec2(120,90)),vec2(100,36)));
 bool close=tap&&menu!=0u&&all(lessThanEqual(abs(p-vec2(492,90)),vec2(30)));
 if((pressed&HB_MINUS)!=0u||badge||close){menu=menu==0u?1u:0u;selected=0u;return -1;}
 if(menu==0u)return -1;
 if((pressed&HB_B)!=0u){menu=0u;return -1;}
 int move=(pressed&HB_UP)!=0u?-1:(pressed&HB_DOWN)!=0u?1:0;
 if(c.atlas[AM+3u]>0u)c.atlas[AM+3u]--;vec2 stick=hbStick(false);
 if(abs(stick.y)>.55&&c.atlas[AM+3u]==0u){move=stick.y>0.0?-1:1;c.atlas[AM+3u]=12u;}
 if(move!=0)selected=uint((int(selected)+move+int(count))%int(count));
 if(tap&&p.x>=40.0&&p.x<=510.0&&p.y>=310.0&&p.y<310.0+float(count)*88.0){selected=uint((p.y-310.0)/88.0);return int(selected);}
 if((pressed&HB_A)!=0u)return int(selected);
 return -1;
}
