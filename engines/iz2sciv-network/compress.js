// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gezine. See LICENSES/Gezine-MIT.txt.
// Adapted from Gezine/CivSwitch: https://github.com/Gezine/CivSwitch/blob/3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f/PicoSwitch/compress.js
// Compress a JS payload for Civ VII chat transport.
// terser → deflateRaw → base25 → chunked img/onerror lines.
//
// Usage: require("./compress")(code) → Promise<string[]>

const zlib = require("zlib");

const MAX = 1000;
const PUNCT = '#%!()+,-.:;=>?@[]^_{|}~';
const DIG = "01";

const DECODER_SRC = `
function b25(s){var P="#%!()+,-.:;=>?@[]^_{|}~",D="01",n=0n,p=1n,q=0,i,c,d,m,x;for(i=s.length-1;i>=0;i--){c=s[i];if(q){d=P.indexOf(c);m=23n;q=0}else{m=25n;x=D.indexOf(c);if(x<0)d=P.indexOf(c);else{d=23+x;q=1}}n+=BigInt(d)*p;p*=m}var o=[];while(n>1n){o.push(Number(n%256n));n/=256n}return o.reverse()}
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
window.run=function(s){var i=s.indexOf("*");if(i>-1)document.body.innerHTML="<pre>star@"+i+":"+s.slice(Math.max(0,i-5),i+6);else eval(str(inflate(b25(s))))};
`;

async function compress(source) {
  const { code } = await require("terser").minify(source, {
    compress: true, mangle: true, module: true,
  });

  const raw = zlib.deflateRawSync(code, { level: 9 });

  // Base25 encode
  let num = 1n;
  for (const b of raw) num = num * 256n + BigInt(b);
  const parts = [];
  let prevD = false;
  while (num > 0n) {
    const m = prevD ? 23n : 25n;
    const digit = Number(num % m);
    num /= m;
    if (prevD) { parts.push(PUNCT[digit]); prevD = false; }
    else if (digit < 23) parts.push(PUNCT[digit]);
    else { parts.push(DIG[digit - 23]); prevD = true; }
  }
  const data = parts.reverse().join("");
  const armor = encodeURIComponent(DECODER_SRC.replace(/\n/g, ""))
    .replace(/'/g, "%27").replace(/\*/g, "%2A").replace(/%2C/g, ",");

  // Chunk into img/onerror lines
  let decChunks, dataChunks, N, w = 0;
  for (;;) {
    w++;
    const d9 = "9".repeat(w);
    const oh = `${d9}/${d9}"></div><img src=x onerror="p=window.p||[],p[${d9}]=` + "`x`\"><div class=\"";
    const size = MAX - oh.length - 1;
    const chop = (s) => {
      const a = [];
      for (let i = 0; i < s.length; i += size) a.push(s.slice(i, i + size));
      return a;
    };
    decChunks = chop(armor);
    dataChunks = chop(data);
    N = decChunks.length + dataChunks.length + 1;
    if (String(N).length <= w && String(Math.max(decChunks.length, dataChunks.length) - 1).length <= w) break;
  }

  const A = decChunks.length;
  const B = dataChunks.length;
  const slot = (slotName, c, k, i) =>
    `${k}/${N}"></div><img src=x onerror="${slotName}=window.${slotName}||[],${slotName}[${i}]=\`${c}\`"><div class="`;

  const lines = [
    ...decChunks.map((c, j) => slot("p", c, j + 1, j)),
    ...dataChunks.map((c, j) => slot("d", c, A + j + 1, j)),
    `${A + B + 1}/${N}"></div><img src=x onerror="if(window.t)return;t=setInterval(()=>{` +
      `if((a=p.join('')).length>=${armor.length})` +
      `if((b=d.join('')).length>=${data.length}){` +
      `clearInterval(t);` +
      `try{eval(decodeURIComponent(a));run(b);}` +
      `catch(e){document.body.innerHTML=e.stack}` +
      `}},30)"><div class="`,
  ];

  const longest = Math.max(...lines.map((l) => l.length + 1));
  if (longest > MAX) throw new Error(`line is ${longest}B > ${MAX}B limit`);

  console.error(`minify ${code.length} → b25 ${data.length} | decoder ${armor.length}`);
  console.error(`${lines.length} lines (${A} decoder + ${B} payload + 1 finale), longest ${longest}`);

  return lines;
}

module.exports = compress;
