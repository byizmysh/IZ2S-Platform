// PC-side WebGL2 encoder. Read back BC1, not the 8x larger RGBA frame.
// Output uses the existing console format and existing shaders unchanged.
(function(root){
 const vertex=`#version 300 es
 void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}`;
 const fragment=`#version 300 es
 precision highp float;precision highp int;
 uniform sampler2D image;uniform vec4 crop;uniform vec2 size;uniform vec2 sourceSize;out vec4 result;
 uint pack565(vec3 p){uvec3 v=uvec3(floor(p*vec3(31.,63.,31.)/255.+.5));return (v.r<<11)|(v.g<<5)|v.b;}
 vec3 unpack565(uint p){return vec3(p>>11,(p>>5)&63u,p&31u)*255./vec3(31.,63.,31.);}
 void main(){
  ivec2 outAt=ivec2(gl_FragCoord.xy),block=ivec2(outAt.x/2,outAt.y);vec3 pixels[16];vec3 lo=vec3(255),hi=vec3(0);
  float scale=min(1.,min(size.x/crop.z,size.y/crop.w));vec2 used=crop.zw*scale,pad=(size-used)*.5;
  for(int y=0;y<4;y++)for(int x=0;x<4;x++){
   vec2 at=vec2(block*4+ivec2(x,y))+.5;vec2 uv=(crop.xy+(at-pad)/scale)/sourceSize;
   vec3 p=any(lessThan(at,pad))||any(greaterThanEqual(at,pad+used))?vec3(0):floor(texture(image,uv).rgb*255.+.5);
   pixels[y*4+x]=p;lo=min(lo,p);hi=max(hi,p);
  }
  vec3 range=hi-lo;int axis=range.r>=range.g&&range.r>=range.b?0:range.g>=range.b?1:2;
  int li=0,hiIndex=0;for(int i=1;i<16;i++){if(pixels[i][axis]<pixels[li][axis])li=i;if(pixels[i][axis]>pixels[hiIndex][axis])hiIndex=i;}
  uint a=pack565(pixels[li]),b=pack565(pixels[hiIndex]);if(a<b){uint t=a;a=b;b=t;}
  vec3 c=unpack565(a),d=unpack565(b)-c;float norm=dot(d,d),factor=norm>0.?3./norm:0.;uint bits=0u;
  for(int i=0;i<16;i++){uint q=uint(clamp(floor(dot(pixels[i]-c,d)*factor+.5),0.,3.)),index=q==0u?0u:q==3u?1u:q+1u;bits|=index<<uint(i*2);}
  uvec4 bytes=(outAt.x&1)==0?uvec4(a&255u,a>>8,b&255u,b>>8):uvec4(bits&255u,(bits>>8)&255u,(bits>>16)&255u,bits>>24);
  result=vec4(bytes)/255.;
 }`;
 function create(){
  const canvas=typeof OffscreenCanvas==='function'?new OffscreenCanvas(1,1):document.createElement('canvas');
  const gl=canvas.getContext('webgl2',{alpha:false,antialias:false,depth:false,stencil:false,preserveDrawingBuffer:false});
  if(!gl)throw Error('PC WebGL2 unavailable');
  const debug=gl.getExtension('WEBGL_debug_renderer_info'),renderer=debug?gl.getParameter(debug.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER);
  if(/swiftshader|llvmpipe|software/i.test(renderer)){gl.getExtension('WEBGL_lose_context')?.loseContext();throw Error('Hardware GPU unavailable');}
  function shader(type,source){const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;}
  const program=gl.createProgram(),vs=shader(gl.VERTEX_SHADER,vertex),fs=shader(gl.FRAGMENT_SHADER,fragment);gl.attachShader(program,vs);gl.attachShader(program,fs);gl.linkProgram(program);gl.deleteShader(vs);gl.deleteShader(fs);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
  const input=gl.createTexture(),output=gl.createTexture(),fbo=gl.createFramebuffer(),vao=gl.createVertexArray();gl.bindVertexArray(vao);gl.useProgram(program);
  gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,input);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  const uniforms=Object.fromEntries(['crop','size','sourceSize'].map(n=>[n,gl.getUniformLocation(program,n)]));let width=0,height=0,busy=false,closed=false;
  gl.disable(gl.DITHER);gl.disable(gl.BLEND);gl.disable(gl.DEPTH_TEST);gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,gl.NONE);
  return {renderer,async encode(source,area,w,h){
   if(closed||busy||w<4||h<4||w>1920||h>1080||w%4||h%4)throw Error('Invalid GPU frame');busy=true;
   try{
    if(gl.isContextLost())throw Error('PC GPU context lost');
    const start=performance.now();gl.bindTexture(gl.TEXTURE_2D,input);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,source);const captureMs=performance.now()-start;
    if(w!==width||h!==height){gl.bindTexture(gl.TEXTURE_2D,output);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,w/2,h/4,0,gl.RGBA,gl.UNSIGNED_BYTE,null);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,output,0);if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('PC GPU output unavailable');width=w;height=h;}
    gl.bindTexture(gl.TEXTURE_2D,input);gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);gl.viewport(0,0,w/2,h/4);gl.uniform4f(uniforms.crop,area.x,area.y,area.w,area.h);gl.uniform2f(uniforms.size,w,h);gl.uniform2f(uniforms.sourceSize,area.sourceWidth,area.sourceHeight);gl.drawArrays(gl.TRIANGLES,0,3);
    // A small BC1 readback synchronizes this draw; no extra timer or frame queue.
    const bytes=new Uint8Array(w*h/2);gl.readPixels(0,0,w/2,h/4,gl.RGBA,gl.UNSIGNED_BYTE,bytes);if(gl.getError()!==gl.NO_ERROR)throw Error('PC GPU readback failed');
    return {bytes,captureMs,encodeMs:performance.now()-start-captureMs};
   }finally{busy=false;}
  },close(){if(closed)return;closed=true;gl.deleteFramebuffer(fbo);gl.deleteTexture(input);gl.deleteTexture(output);gl.deleteVertexArray(vao);gl.deleteProgram(program);gl.getExtension('WEBGL_lose_context')?.loseContext();}};
 }
 async function qualify(codec){
  const source=typeof OffscreenCanvas==='function'?new OffscreenCanvas(16,16):document.createElement('canvas');source.width=16;source.height=16;
  const ctx=source.getContext('2d'),colors=['#f00','#0f0','#00f','#000'],values=[0xf800,0x07e0,0x001f,0];
  for(let i=0;i<4;i++){ctx.fillStyle=colors[i];ctx.fillRect((i&1)*8,(i>>1)*8,8,8);}
  const {bytes}=await codec.encode(source,{x:0,y:0,w:16,h:16,sourceWidth:16,sourceHeight:16},16,16);
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  for(let y=0;y<4;y++)for(let x=0;x<4;x++){const pos=(y*4+x)*8,v=values[(y>>1)*2+(x>>1)];if(view.getUint16(pos,true)!==v||view.getUint16(pos+2,true)!==v||view.getUint32(pos+4,true)!==0)throw Error('PC GPU codec self-test failed');}
  return codec;
 }
 const api={create,qualify};if(typeof module==='object')module.exports=api;else root.IZGpuCodec=api;
})(typeof globalThis==='object'?globalThis:this);
