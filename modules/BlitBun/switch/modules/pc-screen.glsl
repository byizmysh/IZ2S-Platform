// Host writes an inactive RGB565 slot, then publishes. One compute invocation
// latches it at the frame boundary; the host waits for that acknowledgement.
const uint SB=CB+800u;
#ifdef IZ_STREAM_PRIVATE
layout(std430,binding=1) readonly buffer StreamVideo { uint words[]; } video;
const uint VIDEO0=0u;
const uint VIDEO_STRIDE=0x100000u/4u;
#else
const uint VIDEO0=(0x610000u-1024u)/4u;
const uint VIDEO_STRIDE=0x78000u/4u;
#endif
uint streamWord(uint at){
#ifdef IZ_STREAM_PRIVATE
 return video.words[at];
#else
 return c.atlas[at];
#endif
}
