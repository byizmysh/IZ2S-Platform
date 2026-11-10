#!/usr/bin/env node
// compress_stage.js - max-compress a payload JS into the hex-transported
// "stage" (inflate decoder + deflate(base64) + eval line). Self-contained:
// the decoder below is copied verbatim from compress.js's DECODER_SRC
// (inflate + str; the b25 chat transport is not used here). No chat-censor
// armor - the transport is savedata hex.
// usage: node compress_stage.js <input.js> <out.hex> [out_check.js]
const fs = require("fs"), zlib = require("zlib");

const [, , inPath, outHex, outCheck] = process.argv;
if (!inPath || !outHex) { console.error("usage: node compress_stage.js <input.js> <out.hex> [out_check.js]"); process.exit(1); }

const DECODER = `
function inflate(src){
var p=0,out=[];
function bit(){if(p>>3>=src.length)throw'decode';var b=src[p>>3]>>(p&7)&1;p++;return b}
function take(n){var v=0,s=0;while(n--){v|=bit()<<s;s++}return v}
function huff(lens){
var max=0,i,c=0,count=[],next=[],map={};
for(i=0;i<lens.length;i++)if(lens[i]>max)max=lens[i];
for(i=0;i<=max;i++)count[i]=0;
for(i=0;i<lens.length;i++)count[lens[i]]++;
count[0]=0;
for(i=1;i<=max;i++){c=c+count[i-1]<<1;next[i]=c}
for(i=0;i<lens.length;i++)if(lens[i]){map[lens[i]*65536+next[lens[i]]]=i;next[lens[i]]++}
return function(){var code=0,len=0;do{code=code<<1|bit();len++}while(map[len*65536+code]===undefined&&len<=max);return map[len*65536+code]};
}
var LB=[3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258];
var LX=[0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0];
var DB=[1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577];
var DX=[0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13];
var ORD=[16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15],fix=null;
for(;;){
var last=take(1),type=take(2),lit,dis,i;
if(type==0){
p=p+7&-8;var n=src[p>>3]|src[p+8>>3]<<8;p+=32;
while(n--){out.push(src[p>>3]);p+=8}
}else{
if(type==1){
if(!fix){var a=[],b=[];for(i=0;i<288;i++)a[i]=i<144?8:i<256?9:i<280?7:8;for(i=0;i<32;i++)b[i]=5;fix=[huff(a),huff(b)]}
lit=fix[0];dis=fix[1];
}else{
var hlit=take(5)+257,hdist=take(5)+1,hclen=take(4)+4,cl=[];
for(i=0;i<19;i++)cl[i]=0;
for(i=0;i<hclen;i++)cl[ORD[i]]=take(3);
var ch=huff(cl),lens=[],total=hlit+hdist;
while(lens.length<total){
var s=ch();
if(s<16)lens.push(s);
else if(s==16){var r=3+take(2),prev=lens[lens.length-1];while(r--)lens.push(prev)}
else{var r=s==17?3+take(3):11+take(7);while(r--)lens.push(0)}
}
lit=huff(lens.slice(0,hlit));dis=huff(lens.slice(hlit));
}
for(;;){
var s=lit();
if(s<256){out.push(s);continue}
if(s==256)break;
s-=257;
var len=LB[s]+take(LX[s]),ds=dis(),dd=DB[ds]+take(DX[ds]),from=out.length-dd;
while(len--)out.push(out[from++]);
}
}
if(last)break;
}
return out;
}
function str(a){
var CHUNK=8192,parts=[],buf=[],i,c,n=a.length;
for(i=0;i<n;i++){
c=a[i];
if(c>223)c=(c&15)<<12|(a[++i]&63)<<6|a[++i]&63;
else if(c>191)c=(c&31)<<6|a[++i]&63;
buf.push(c);
if(buf.length===CHUNK){parts.push(String.fromCharCode.apply(null,buf));buf.length=0}
}
if(buf.length)parts.push(String.fromCharCode.apply(null,buf));
return parts.join('');
}
function B64(s){
var A="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",m={},i;
for(i=0;i<64;i++)m[A[i]]=i;
var out=[],v=0,bits=0,k;
s=s.replace(/=+$/,"");
for(k=0;k<s.length;k++){
v=v<<6|m[s[k]];bits+=6;
if(bits>=8){bits-=8;out.push(v>>bits&255)}
}
return out;
}
`;

(async () => {
  const src = fs.readFileSync(inPath, "utf8");
  const ascii = src.replace(/[^\x00-\x7F]/g, "");   // non-ASCII only allowed in comments
  const { code } = await require("terser").minify(ascii, {
    module: true, mangle: true, compress: { passes: 3, drop_debugger: true },
  });
  if (outCheck) fs.writeFileSync(outCheck, code);
  const raw = zlib.deflateRawSync(code, { level: 9 });
  const b64 = raw.toString("base64");
  const stage = DECODER + "\neval(str(inflate(B64(\"" + b64 + "\"))));";
  fs.writeFileSync(outHex, Buffer.from(stage, "latin1").toString("hex"));
  // offline verification: the stage must inflate back to the minified code
  const vm = require("vm");
  const sb = { console };
  vm.createContext(sb);
  vm.runInContext('var __o;eval=x=>__o=x;' + stage, sb, { timeout: 30000 });
  console.log(`core ${ascii.length} -> terser ${code.length} -> deflate ${raw.length} -> stage-hex ${stage.length * 2}`);
  console.log("offline decode: " + (sb.__o === code ? "EXACT" : "MISMATCH"));
  if (sb.__o !== code) process.exit(2);
})();
