// Keep in sync with homebrew-launcher/controller-hub-layout.cjs (cross-tested).
const uint CH_BASE=0x701c00u,CH_BYTES=256u,CH=(CH_BASE-1024u)/4u,CH_MAGIC=0x48435a49u,CH_VERSION=1u;
const uint CH_MAGIC_AT=0u,CH_VERSION_AT=1u,CH_REQUEST=2u,CH_ACK=3u,CH_REQUEST_SOURCE=4u,CH_REQUEST_PROFILE=5u,CH_STATUS=6u,CH_SOURCE=7u,CH_PROFILE=8u,CH_CONNECTED=9u,CH_CURSOR=10u,CH_TOUCH=11u,CH_REPEAT=12u,CH_SCROLL=13u,CH_EFFECTIVE=14u,CH_CAPABILITIES=15u,CH_CONTROLLER_COUNT=16u,CH_CONTROLLERS=17u,CH_HEARTBEAT=36u;
bool controllerHubReady(){return c.atlas[CH+CH_MAGIC_AT]==CH_MAGIC&&c.atlas[CH+CH_VERSION_AT]==CH_VERSION;}
uint controllerHubCount(){return controllerHubReady()?min(c.atlas[CH+CH_CONTROLLER_COUNT],9u):0u;}
uint controllerHubId(uint i){return c.atlas[CH+CH_CONTROLLERS+i*2u];}
uint controllerHubStyle(uint i){return c.atlas[CH+CH_CONTROLLERS+i*2u+1u];}
uint controllerHubSourceRows(){return 1u+controllerHubCount();}
uint controllerHubRowCount(){return controllerHubSourceRows()+3u;}
uint controllerHubRow(uint ordinal){
 uint count=controllerHubCount();if(ordinal==0u)return 0u;
 if(ordinal<=count){uint i=ordinal-1u;return ((controllerHubId(i)+1u)<<8u)|((controllerHubStyle(i)&1u)!=0u?1u:2u);}
 return ordinal==count+1u?10u:ordinal==count+2u?11u:12u;
}
uint controllerHubOrdinal(uint row){for(uint i=0u;i<controllerHubRowCount();i++)if(controllerHubRow(i)==row)return i;return 0u;}
bool controllerHubIsSource(uint row){return row==0u||row>255u;}
float controllerHubRowY(uint ordinal){uint count=controllerHubRowCount(),scroll=min(c.atlas[CH+CH_SCROLL],count>7u?count-7u:0u);return 336.0+float(int(ordinal)-int(scroll))*78.0;}
