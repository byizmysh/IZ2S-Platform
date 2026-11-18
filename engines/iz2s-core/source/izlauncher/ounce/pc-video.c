#include "pc-video.h"
#include <sb4/native-app.h>
#include <sb4/stream.h>
#include <string.h>

static uint32_t read_word(const uint8_t *bytes){
    return (uint32_t)bytes[0]<<24|(uint32_t)bytes[1]<<16|(uint32_t)bytes[2]<<8|bytes[3];
}

static void write_word(uint8_t *bytes,uint32_t value){
    for(unsigned i=0;i<4;++i)bytes[i]=(uint8_t)(value>>(24-8*i));
}

static bool same_hash(const uint8_t *left,const uint8_t *right){
    unsigned differences=0;
    for(unsigned i=0;i<32;++i)differences|=left[i]^right[i];
    return differences==0;
}

static bool receive(const IzPcVideoIo *io,uint8_t operation,uint32_t seen,uint32_t heard,bool media,const uint8_t *input,
                    uint8_t *body,size_t capacity,uint8_t *status,uint32_t *length){
    if(!io||!io->transfer||!io->sign||!io->hash||!body)return false;
    uint8_t message[41+IZ_PC_INPUT_BYTES],request[41+IZ_PC_INPUT_BYTES],reply[69],digest[32];
    if(!io->transfer(io->context,message,32,false))return false;
    const size_t prefix=(media?9:5)+(input?IZ_PC_INPUT_BYTES:0);
    request[0]=operation;
    write_word(request+1,seen);
    if(media)write_word(request+5,heard);
    if(input)memcpy(request+9,input,IZ_PC_INPUT_BYTES);
    memcpy(message+32,request,prefix);
    io->sign(io->context,request+prefix,message,32+prefix);
    if(!io->transfer(io->context,request,prefix+32,true)||
       !io->transfer(io->context,reply,sizeof reply,false))return false;

    memcpy(message+32,reply,37);
    io->sign(io->context,digest,message,69);
    if(!same_hash(digest,reply+37))return false;
    *status=reply[0];
    *length=read_word(reply+1);
    if(!*length)return true;
    if(*status!=(media?6:4)||*length>capacity||!io->transfer(io->context,body,*length,false))return false;
    io->hash(digest,body,*length);
    return same_hash(digest,reply+5);
}

static bool video_header(const uint8_t *header,const uint8_t *pixels,size_t bytes,uint32_t seen,IzPcVideoFrame *out){
    const IzPcVideoFrame decoded={.sequence=read_word(header),.width=read_word(header+4),
        .height=read_word(header+8),.format=read_word(header+12),.pixels=pixels,.bytes=bytes};
    if(!decoded.sequence||decoded.sequence==seen||
       sb4StreamFrameBytes(decoded.width,decoded.height,decoded.format)!=decoded.bytes)return false;
    *out=decoded;
    return true;
}

enum IzPcVideoResult iz_pc_video_receive(const IzPcVideoIo *io,uint8_t operation,uint32_t seen,
                                        uint8_t *body,size_t capacity,IzPcVideoFrame *frame){
    if(!frame)return IZ_PC_VIDEO_ERROR;
    *frame=(IzPcVideoFrame){0};
    if(operation!='V'&&operation!='W')return IZ_PC_VIDEO_ERROR;
    if(capacity>IZ_PC_VIDEO_CAPACITY)capacity=IZ_PC_VIDEO_CAPACITY;
    uint8_t status;
    uint32_t length;
    if(!receive(io,operation,seen,0,false,NULL,body,capacity,&status,&length))return IZ_PC_VIDEO_ERROR;
    if(!length&&status==0)return IZ_PC_VIDEO_EMPTY;
    if(!length&&status==5&&seen)return IZ_PC_VIDEO_CURRENT;
    if(status!=4||length<=16||!video_header(body,body+16,length-16,seen,frame))return IZ_PC_VIDEO_ERROR;
    return IZ_PC_VIDEO_FRAME;
}

enum IzPcVideoResult iz_pc_media_receive(const IzPcVideoIo *io,uint8_t operation,uint32_t seen,uint32_t heard,
                                        const uint8_t input[IZ_PC_INPUT_BYTES],uint8_t *body,size_t capacity,IzPcMediaFrame *frame){
    if(!frame)return IZ_PC_VIDEO_ERROR;
    *frame=(IzPcMediaFrame){0};
    if(input?(operation!='Y'&&operation!='Z'&&operation!='y'):(operation!='M'&&operation!='N'&&operation!='a'&&operation!='b'))return IZ_PC_VIDEO_ERROR;
    if(capacity>IZ_PC_MEDIA_CAPACITY)capacity=IZ_PC_MEDIA_CAPACITY;
    uint8_t status;
    uint32_t length;
    if(!receive(io,operation,seen,heard,true,input,body,capacity,&status,&length))return IZ_PC_VIDEO_ERROR;
    if(!length&&status==0)return IZ_PC_VIDEO_EMPTY;
    if(!length&&status==5&&seen)return IZ_PC_VIDEO_CURRENT;
    if(status!=6||length<=28)return IZ_PC_VIDEO_ERROR;
    const uint32_t video_bytes=read_word(body+16),audio_sequence=read_word(body+20),audio_bytes=read_word(body+24);
    const uint32_t max_audio=operation=='b'?IZ_PC_PCM_BATCH*IZ_PC_PCM_BYTES:IZ_PC_PCM_BYTES;
    if(video_bytes>length-28||audio_bytes!=length-28-video_bytes||
       audio_bytes>max_audio||audio_bytes%IZ_PC_PCM_BYTES||
       (!!audio_sequence!=!!audio_bytes)||(audio_sequence&&audio_sequence==heard)||
       ((operation=='a'||operation=='b')&&video_bytes)||(operation=='y'&&audio_bytes))return IZ_PC_VIDEO_ERROR;
    IzPcMediaFrame decoded={0};
    if(video_bytes){
        if(!video_header(body,body+28,video_bytes,seen,&decoded.video))return IZ_PC_VIDEO_ERROR;
    }else if(!seen||read_word(body)||read_word(body+4)||read_word(body+8)||read_word(body+12))return IZ_PC_VIDEO_ERROR;
    if(audio_bytes){decoded.audio_sequence=audio_sequence;decoded.pcm=body+28+video_bytes;decoded.pcm_bytes=audio_bytes;}
    *frame=decoded;
    return video_bytes?IZ_PC_VIDEO_FRAME:IZ_PC_VIDEO_CURRENT;
}

static uint32_t local_word(const uint8_t *bytes,size_t offset){
    uint32_t value;
    memcpy(&value,bytes+offset,sizeof value);
    return value;
}

static void local_put(uint8_t *bytes,size_t offset,uint32_t value){
    memcpy(bytes+offset,&value,sizeof value);
}

void iz_pc_capture_controls(const IzNativeApp *app,uint8_t packet[IZ_PC_INPUT_BYTES]){
    enum { VideoHeader=0x700c80, VideoMode=0x700004, Touch=128 };
    memset(packet,0,IZ_PC_INPUT_BYTES);
    if(!app||!app->controls||app->controls_bytes<VideoHeader+26*sizeof(uint32_t))return;
    const uint8_t *controls=app->controls;
    const uint32_t mode=local_word(controls,VideoMode),menu=local_word(controls,VideoHeader+25*4);
    local_put(packet,328,mode);
    local_put(packet,336,menu);
    if(mode!=2||menu)return;
    const uint32_t attached=local_word(controls,32)&1u;
    local_put(packet,332,(local_word(controls,VideoHeader+24*4)&~2u)|(attached?2u:0u));
    if(attached){
        memcpy(packet+16,controls+8,24); /* Buttons and four normalized axes. */
        local_put(packet,40,1);
    }
    local_put(packet,348,local_word(controls,Touch+8));
    local_put(packet,364,local_word(controls,Touch+24));
    local_put(packet,372,local_word(controls,Touch+32));
    local_put(packet,376,local_word(controls,Touch+36));
}
