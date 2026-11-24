// Two float32 components keep twelve display digits without requiring shaderFloat64.
vec2 dsAdd(vec2 a,vec2 b){
 precise float s=a.x+b.x,v=s-a.x,e=(a.x-(s-v))+(b.x-v)+a.y+b.y,h=s+e;
 return vec2(h,e-(h-s));
}
vec2 dsMul(vec2 a,vec2 b){
 precise float sa=a.x*4097.0,ah=sa-(sa-a.x),al=a.x-ah;
 precise float sb=b.x*4097.0,bh=sb-(sb-b.x),bl=b.x-bh;
 precise float p=a.x*b.x,e=((ah*bh-p)+ah*bl+al*bh)+al*bl+a.x*b.y+a.y*b.x+a.y*b.y,h=p+e;
 return vec2(h,e-(h-p));
}
vec2 dsDiv(vec2 a,vec2 b){
 precise float q=a.x/b.x;vec2 r=dsAdd(a,-dsMul(b,vec2(q,0)));
 return dsAdd(vec2(q,0),vec2((r.x+r.y)/b.x,0));
}
const uint FRESH=1u,ERROR=2u,EQUALS=4u,ENTRY=8u;
void zeroDisplay(){c.rows[0]=48u;c.piece.x=1;c.piece.y=0;c.flow.xy=vec2(0);}
void clearCalc(){zeroDisplay();c.flow.zw=vec2(0);c.game.w=0u;c.bag[0]=0u;c.game.z=FRESH;}
void failCalc(){
 c.game.z=FRESH|ERROR;c.piece.x=5;
 c.rows[0]=69u;c.rows[1]=82u;c.rows[2]=82u;c.rows[3]=79u;c.rows[4]=82u;
}
bool validNumber(vec2 v){return !any(isnan(v))&&!any(isinf(v));}
void appendChar(uint ch){if(c.piece.x<20)c.rows[c.piece.x++]=ch;}
void showNumber(){
 vec2 v=c.flow.xy;if(!validNumber(v)){failCalc();return;}
 if(v.x==0.0){zeroDisplay();return;}
 bool negative=v.x<0.0;if(negative)v=-v;
 int exponent=int(floor(log(v.x)/log(10.0)));vec2 power=vec2(1,0);
 for(int i=0;i<abs(exponent);i++)power=dsMul(power,vec2(10,0));
 vec2 n=exponent>=0?dsDiv(v,power):dsMul(v,power);
 if(n.x>10.0||(n.x==10.0&&n.y>=0.0)){n=dsDiv(n,vec2(10,0));exponent++;}
 if(n.x<1.0||(n.x==1.0&&n.y<0.0)){n=dsMul(n,vec2(10,0));exponent--;}
 int digits[12];
 for(int i=0;i<12;i++){
  int d=int(floor(n.x));if(n.x==float(d)&&n.y<0.0)d--;
  d=clamp(d,0,9);digits[i]=d;n=dsMul(dsAdd(n,vec2(-float(d),0)),vec2(10,0));
 }
 if(n.x>5.0||(n.x==5.0&&n.y>=0.0)){
  bool carry=true;
  for(int i=11;i>=0;i--){if(!carry)break;digits[i]++;carry=digits[i]==10;if(carry)digits[i]=0;}
  if(carry){digits[0]=1;exponent++;}
 }
 int count=12;while(count>1&&digits[count-1]==0)count--;
 c.piece.x=0;c.piece.y=0;if(negative)appendChar(45u);
 if(exponent>=0&&exponent<12){
  for(int i=0;i<=exponent;i++)appendChar(i<count?uint(digits[i])+48u:48u);
  if(count>exponent+1){appendChar(46u);for(int i=exponent+1;i<count;i++)appendChar(uint(digits[i])+48u);}
 }else if(exponent<0&&exponent>=-6){
  appendChar(48u);appendChar(46u);for(int i=0;i<-exponent-1;i++)appendChar(48u);
  for(int i=0;i<count;i++)appendChar(uint(digits[i])+48u);
 }else{
  appendChar(uint(digits[0])+48u);
  if(count>1){appendChar(46u);for(int i=1;i<count;i++)appendChar(uint(digits[i])+48u);}
  appendChar(69u);appendChar(exponent<0?45u:43u);int e=abs(exponent);
  if(e>=10)appendChar(uint(e/10)+48u);appendChar(uint(e%10)+48u);
 }
}
void parseInput(){
 vec2 value=vec2(0),power=vec2(1,0);bool decimal=false;int start=c.rows[0]==45u?1:0;
 for(int i=start;i<c.piece.x;i++){
  if(c.rows[i]==46u){decimal=true;continue;}
  value=dsAdd(dsMul(value,vec2(10,0)),vec2(float(c.rows[i]-48u),0));
  if(decimal)power=dsMul(power,vec2(10,0));
 }
 c.flow.xy=dsDiv(value,power);if(start==1)c.flow.xy=-c.flow.xy;
 c.piece.y=decimal?1:0;c.game.z=ENTRY;
}
void prepareInput(){
 if((c.game.z&ERROR)!=0u)clearCalc();
 if((c.game.z&FRESH)!=0u){zeroDisplay();if((c.game.z&EQUALS)!=0u){c.game.w=0u;c.bag[0]=0u;}c.game.z=ENTRY;}
}
void enterDigit(uint digit){
 prepareInput();int start=c.rows[0]==45u?1:0;
 if(c.piece.x==start+1&&c.rows[start]==48u)c.rows[start]=digit+48u;
 else{int count=0;bool significant=false;for(int i=start;i<c.piece.x;i++)if(c.rows[i]!=46u){if(c.rows[i]!=48u)significant=true;if(significant)count++;}
  if(count>=12||c.piece.x>=19)return;appendChar(digit+48u);}
 parseInput();
}
vec2 calculate(vec2 a,vec2 b,uint operation){
 if(operation==1u)return dsAdd(a,b);if(operation==2u)return dsAdd(a,-b);
 if(operation==3u)return dsMul(a,b);
 if(b.x==0.0){failCalc();return vec2(0);}return dsDiv(a,b);
}
void useOperator(uint operation){
 if((c.game.z&ERROR)!=0u)return;
 if(c.game.w!=0u&&(c.game.z&ENTRY)!=0u){
  c.flow.xy=calculate(c.flow.zw,c.flow.xy,c.game.w);if((c.game.z&ERROR)!=0u)return;
  showNumber();if((c.game.z&ERROR)!=0u)return;
 }
 c.flow.zw=c.flow.xy;c.game.w=operation;c.game.z=FRESH;c.bag[0]=0u;
}
void equals(){
 if((c.game.z&ERROR)!=0u)return;uint operation=c.game.w;
 if(operation!=0u){
  c.bagMeta.zw=floatBitsToUint(c.flow.xy);c.bag[0]=operation;
  c.flow.xy=calculate(c.flow.zw,c.flow.xy,operation);c.game.w=0u;
 }else if((c.game.z&EQUALS)!=0u&&c.bag[0]!=0u)c.flow.xy=calculate(c.flow.xy,uintBitsToFloat(c.bagMeta.zw),c.bag[0]);
 if((c.game.z&ERROR)!=0u)return;c.game.z=FRESH|EQUALS|ENTRY;showNumber();
}
void activateKey(int id){
 c.stats.x=uint(id);c.stats.z=8u;
 if(id>=10&&id<=12){enterDigit(uint(id-3));return;}
 if(id>=15&&id<=17){enterDigit(uint(id-11));return;}
 if(id>=20&&id<=22){enterDigit(uint(id-19));return;}
 if(id==25){enterDigit(0u);return;}
 if(id==26){prepareInput();if(c.piece.y==0&&c.piece.x<19){appendChar(46u);parseInput();}return;}
 if(id==7){clearCalc();return;}
 if(id==6){if((c.game.z&ERROR)!=0u)clearCalc();else{zeroDisplay();c.game.z=FRESH|ENTRY;}return;}
 if((c.game.z&ERROR)!=0u)return;
 if(id==0){c.bagMeta.xy=uvec2(0);c.bag[1]=0u;return;}
 if(id==1){c.flow.xy=uintBitsToFloat(c.bagMeta.xy);c.game.z=FRESH|ENTRY;showNumber();return;}
 if(id>=2&&id<=4){
  vec2 memory=id==2?c.flow.xy:dsAdd(uintBitsToFloat(c.bagMeta.xy),id==3?c.flow.xy:-c.flow.xy);
  if(!validNumber(memory)){failCalc();return;}c.bagMeta.xy=floatBitsToUint(memory);c.bag[1]=1u;return;
 }
 if(id==5){
  if((c.game.z&FRESH)!=0u)return;c.piece.x--;
  if(c.piece.x==0||(c.piece.x==1&&c.rows[0]==45u))zeroDisplay();parseInput();return;
 }
 if(id==8){
  if((c.game.z&FRESH)!=0u&&c.flow.x!=0.0){c.flow.xy=-c.flow.xy;c.game.z=FRESH|ENTRY;showNumber();return;}
  prepareInput();
  if(c.rows[0]==45u){for(int i=0;i<c.piece.x-1;i++)c.rows[i]=c.rows[i+1];c.piece.x--;}
  else{if(c.piece.x>=19)return;for(int i=c.piece.x;i>0;i--)c.rows[i]=c.rows[i-1];c.rows[0]=45u;c.piece.x++;}
  parseInput();return;
 }
 if(id==9||id==19||id==14){
  if(id==9){if(c.flow.x<0.0){failCalc();return;}if(c.flow.x!=0.0){vec2 r=vec2(sqrt(c.flow.x),0);for(int i=0;i<2;i++)r=dsMul(dsAdd(r,dsDiv(c.flow.xy,r)),vec2(.5,0));c.flow.xy=r;}}
  if(id==19){if(c.flow.x==0.0){failCalc();return;}c.flow.xy=dsDiv(vec2(1,0),c.flow.xy);}
  if(id==14){c.flow.xy=dsDiv(c.flow.xy,vec2(100,0));if(c.game.w==1u||c.game.w==2u)c.flow.xy=dsMul(c.flow.zw,c.flow.xy);}
  c.game.z=FRESH|ENTRY;showNumber();return;
 }
 if(id==24){equals();return;}
 useOperator(id==27?1u:id==23?2u:id==18?3u:4u);
}
