/* Launcher-owned reader controller. The qualified application's GPU shaders,
 * glyph atlas and controls are unchanged. No host callbacks or worker threads. */
#include "format.h"
typedef uint32_t U;typedef uint64_t Q;typedef uint8_t B;
#define FAIL 0xe820u
enum {IDLE,READING,LAYOUT};
typedef struct {
 U magic,stage,stopped,selected,target,last_seq,prefs[7],positions[READER_BOOKS];
 U dirty,save_delay,loaded,crc,cursor,line_at,line_chars,line_width,word;
 U line_count,output,rows,limit,face,source_position,generation,page;
 U dict_at[128],dict_len[128],dict_hash[256];
 char line[192];B save[4096];ReaderLine lines[READER_LINES];B payload[READER_PAYLOAD];
} State;
_Static_assert(sizeof(State)+32768u<READER_DATA,"Reader working memory budget");
static void copy(void*d,const void*s,U n){B*a=d;const B*b=s;for(U i=0;i<n;i++)a[i]=b[i];}
static void zero(void*d,U n){B*a=d;for(U i=0;i<n;i++)a[i]=0;}
static int equal(const void*a,const void*b,U n){const B*x=a,*y=b;for(U i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static U minimum(U a,U b){return a<b?a:b;}
static U crc(U c,const B*b,U n){for(U i=0;i<n;i++){c^=b[i];for(U k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1));}return c;}
static U*control(IzNativeApp*a,U at){return (U*)((B*)a->controls+at);}
static U*gpu(IzNativeApp*a){return control(a,1024);}
static ReaderBook*books(ReaderConfig*p){return (void*)((B*)p+64);}
static ReaderAsset*assets(ReaderConfig*p){return (void*)(books(p)+p->books);}
static int prefs_valid(const U*p){return p[0]<9&&p[1]<2&&p[2]<2&&p[3]<3&&p[4]<3&&p[5]<3&&p[6]<3;}
static int is_letter(U c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='\'';}
static U hash(const B*p,U n){U h=2166136261u;for(U i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
static int validate(IzNativeApp*a,ReaderConfig*p){
 if(a->data_bytes<64||p->magic!=READER_CONFIG||p->version!=1||p->reserved||!p->books||p->books>128||!p->assets||p->assets>16||p->initial_book>=p->books||p->initial_open>1||!prefs_valid(p->prefs))return 0;
 if(p->bytes!=64+p->books*128+p->assets*88||p->state_offset!=((p->bytes+15)&~15u)||(Q)p->state_offset+sizeof(State)>a->data_bytes)return 0;
 ReaderBook*b=books(p);ReaderAsset*r=assets(p);
 for(U i=0;i<p->assets;i++){
  const char*s=r[i].path;if(!r[i].bytes||r[i].bytes>256u*1024*1024||r[i].reserved)return 0;
  const char*prefix="asset-",*suffix=".izdata";if(!equal(s,prefix,6)||!equal(s+70,suffix,7)||s[77]||s[78]||s[79])return 0;
  for(U k=6;k<70;k++)if(!((s[k]>='0'&&s[k]<='9')||(s[k]>='a'&&s[k]<='f')))return 0;
 }
 for(U i=0;i<p->books;i++){
  if(b[i].asset>=p->assets||b[i].bytes<5||b[i].bytes>READER_PAYLOAD||b[i].text_at<4||b[i].text_at>8192||!b[i].text_bytes||b[i].text_bytes>READER_TEXT||(Q)b[i].text_at+b[i].text_bytes!=b[i].bytes||b[i].dictionary>128||b[i].position>b[i].text_bytes||b[i].reserved[0]||b[i].reserved[1])return 0;
  if(b[i].offset>r[b[i].asset].bytes||b[i].bytes>r[b[i].asset].bytes-b[i].offset||b[i].title[63])return 0;
  for(U k=0;k<24;k++)if(!((b[i].id[k]>='0'&&b[i].id[k]<='9')||(b[i].id[k]>='a'&&b[i].id[k]<='f')))return 0;
  for(U j=0;j<i;j++)if(equal(b[i].id,b[j].id,24))return 0;
 }
 U*g=gpu(a);if(g[0]!=0x424f4f4bu||g[1]!=1||g[2]!=131072||g[3]!=1024||g[4]!=1920||g[5]!=64)return 0;
 for(U i=64;i<64+384;i++)if(!g[i]||g[i]>16384)return 0;return 1;
}
static void settings(IzNativeApp*a,State*s){
 const U sizes[]={24,28,32,36,40,44,48,56,64},margins[]={48,80,120},spacings[]={125,145,165},rotations[]={0,90,270};U*g=gpu(a);
 g[16]=sizes[s->prefs[0]];g[17]=s->prefs[1];g[18]=s->prefs[2];g[19]=rotations[s->prefs[4]];g[20]=s->prefs[5];g[21]=s->prefs[3];g[22]=margins[s->prefs[6]];
 g[26]=g[19]?1080:1920;g[27]=g[19]?1920:1080;s->face=g[18]*2+g[17];s->limit=(g[26]-2*g[22])*3072;s->rows=minimum(64,(g[27]-230)*100/(g[16]*spacings[g[21]]));if(!s->rows)s->rows=1;
}
static void acknowledge(IzNativeApp*a){*control(a,812)=0;}
static void failed(IzNativeApp*a,State*s){s->stage=IDLE;s->selected=READER_NONE;gpu(a)[31]=READER_NONE;*control(a,788)=0;*control(a,892)=1;acknowledge(a);}
static int save_state(IzNativeApp*a,ReaderConfig*p,State*s){
 if(!s->dirty)return 1;zero(s->save,4096);U*w=(void*)s->save;w[0]=READER_SAVE;w[1]=1;w[2]=4096;copy(s->save+16,s->prefs,28);if(s->selected<p->books)copy(s->save+44,books(p)[s->selected].id,24);w[17]=p->books;
 for(U i=0;i<p->books;i++){copy(s->save+72+i*28,books(p)[i].id,24);*(U*)(s->save+96+i*28)=s->positions[i];}w[3]=~crc(0xffffffffu,s->save+16,4080);
 if(a->write_file(a,"reader-state.bin",s->save,4096)){*control(a,892)=1;return 0;}s->dirty=0;s->save_delay=0;return 1;
}
static void restore(IzNativeApp*a,ReaderConfig*p,State*s){
 U actual=0,rc=a->read_file(a,"reader-state.bin",0,s->save,4096,&actual);if(rc==0x202)return;if(rc){*control(a,892)=1;return;}
 U*w=(void*)s->save;if(actual!=4096||w[0]!=READER_SAVE||w[1]!=1||w[2]!=4096||w[3]!=~crc(0xffffffffu,s->save+16,4080)||!prefs_valid(w+4)||w[17]>128){*control(a,892)=1;return;}
 copy(s->prefs,w+4,28);s->target=READER_NONE;
 for(U i=0;i<p->books;i++){
  if(equal(s->save+44,books(p)[i].id,24))s->target=i;
  for(U j=0;j<w[17];j++)if(equal(s->save+72+j*28,books(p)[i].id,24))s->positions[i]=minimum(*(U*)(s->save+96+j*28),books(p)[i].text_bytes);
 }
}
static int dictionary(IzNativeApp*a,ReaderBook*b,State*s){
 U at=4;zero(s->dict_hash,sizeof s->dict_hash);if(*(U*)s->payload!=b->dictionary)return 0;U*g=gpu(a),out=READER_DICTIONARY;zero((B*)g+READER_DICTIONARY_META,1024);
 for(U i=0;i<b->dictionary;i++){
  if(at>b->text_at||b->text_at-at<4)return 0;U n=*(U*)(s->payload+at);at+=4;if(!n||n>25||n>b->text_at-at||out+n>READER_DICTIONARY_META)return 0;
  for(U j=0;j<n;j++)if(!is_letter(s->payload[at+j])&&!(j==n-1&&s->payload[at+j]==' '))return 0;
  U h=hash(s->payload+at,n)&255;while(s->dict_hash[h]){U j=s->dict_hash[h]-1;if(s->dict_len[j]==n&&equal(s->payload+s->dict_at[j],s->payload+at,n))return 0;h=(h+1)&255;}
  s->dict_hash[h]=i+1;s->dict_at[i]=at;s->dict_len[i]=n;g[READER_DICTIONARY_META/4+i*2]=out;g[READER_DICTIONARY_META/4+i*2+1]=n;copy((B*)g+out,s->payload+at,n);out+=n;at=(at+n+3)&~3u;
 }return at==b->text_at;
}
static int emit(IzNativeApp*a,ReaderBook*b,State*s,int blank){
 if(!s->line_chars&&!blank)return 1;if(s->line_count>=READER_LINES)return 0;
 B out[192];U bytes=0;
 for(U i=0;i<s->line_chars;){U n=0,token=READER_NONE;if(is_letter((B)s->line[i])){while(i+n<s->line_chars&&n<24&&is_letter((B)s->line[i+n]))n++;if(n<2)n=0;else if(i+n<s->line_chars&&s->line[i+n]==' ')n++;}
  if(n){U h=hash((B*)s->line+i,n)&255;while(s->dict_hash[h]){U j=s->dict_hash[h]-1;if(s->dict_len[j]==n&&equal(s->payload+s->dict_at[j],s->line+i,n)){token=j;break;}h=(h+1)&255;}}
  if(token!=READER_NONE){out[bytes++]=(B)(128+token);i+=n;}else if(n){copy(out+bytes,s->line+i,n);bytes+=n;i+=n;}else out[bytes++]=(B)s->line[i++];
 }
 U end=(s->output+bytes+3)&~3u,next=s->line_count+1,pages=(next+s->rows-1)/s->rows;
 if((Q)end+(Q)next*12+(Q)pages*12>READER_GPU)return 0;
 ReaderLine*l=s->lines+s->line_count++;l->offset=s->output;l->bytes=bytes;l->position=s->line_at;copy((B*)gpu(a)+s->output,out,bytes);s->output=end;s->line_chars=s->line_width=0;s->line_at=s->cursor;(void)b;return 1;
}
static void begin_layout(IzNativeApp*a,ReaderConfig*p,State*s){
 s->stage=LAYOUT;s->cursor=s->line_at=s->line_chars=s->line_width=s->line_count=s->word=0;s->output=READER_OUTPUT;*control(a,788)=0;settings(a,s);s->source_position=s->positions[s->target];
 gpu(a)[31]=s->target;*control(a,792)=s->target;copy((B*)gpu(a)+READER_TITLE,books(p)[s->target].title,64);
}
static void begin_read(IzNativeApp*a,State*s,U index){s->target=index;s->stage=READING;s->loaded=0;s->crc=0xffffffffu;*control(a,788)=0;*control(a,812)=1;*control(a,892)=0;}
static int layout(IzNativeApp*a,ReaderBook*b,State*s){
 const B*text=s->payload+b->text_at;U*g=gpu(a),budget=4096;
 while(s->cursor<b->text_bytes&&budget){
  U c=text[s->cursor];if(c=='\n'){
   if(s->cursor+1>=b->text_bytes||text[s->cursor+1]!='\n'||(s->cursor+2<b->text_bytes&&text[s->cursor+2]=='\n'))return 0;
   if(!emit(a,b,s,0))return 0;s->line_at=s->cursor;if(!emit(a,b,s,1))return 0;s->cursor+=2;s->line_at=s->cursor;s->word=0;budget--;continue;
  }if(c<32||c>126)return 0;
  if(!s->word){U n=0,width=0;while(s->cursor+n<b->text_bytes&&n<191&&text[s->cursor+n]!='\n'){U ch=text[s->cursor+n];if(ch<32||ch>126)return 0;width+=g[64+s->face*96+ch-32]*g[16];n++;if(ch==' ')break;}
   if(s->line_chars&&(s->line_width+width>s->limit||s->line_chars+n>190)&&!emit(a,b,s,0))return 0;s->word=1;
  }
  U advance=g[64+s->face*96+c-32]*g[16];if(s->line_chars&&(s->line_width+advance>s->limit||s->line_chars>=190)&&!emit(a,b,s,0))return 0;
  if(s->line_chars||c!=' '){s->line[s->line_chars++]=(char)c;s->line_width+=advance;}s->cursor++;budget--;if(c==' ')s->word=0;
 }
 if(s->cursor<b->text_bytes)return 1;if(!emit(a,b,s,0)||!s->line_count)return 0;
 U lines_at=s->output,pages_at=lines_at+s->line_count*12,pages=(s->line_count+s->rows-1)/s->rows;
 /* Publishing the final tables is also cooperative: line metadata can be
  * several MiB. Use the same FRAME budget, with stage cursor beyond text. */
 s->cursor=b->text_bytes+1;s->loaded=0;s->crc=lines_at;s->line_at=pages_at;s->word=pages;s->page=0;return 2;
}
static int publish(IzNativeApp*a,ReaderBook*b,State*s){
 U*g=gpu(a),line_bytes=s->line_count*12,n=minimum(65536,line_bytes-s->loaded);copy((B*)g+s->crc+s->loaded,(B*)s->lines+s->loaded,n);s->loaded+=n;if(s->loaded<line_bytes)return 1;
 U first=s->line_chars,page=s->line_width,done=0;/* Reuse cleared layout scratch for a cooperative page table cursor. */
 while(first<s->line_count&&done++<1024){U*out=(void*)((B*)g+s->line_at+page*12);out[0]=first;out[1]=minimum(s->rows,s->line_count-first);out[2]=s->lines[first].position;if(out[2]<=s->source_position)s->page=page;first+=s->rows;page++;}
 s->line_chars=first;s->line_width=page;if(first<s->line_count)return 1;
 g[6]=READER_DICTIONARY_META/4;g[8]=s->crc/4;g[9]=s->line_at/4;g[11]=s->word;g[12]=s->line_count;g[23]=1024;g[24]=13312;g[28]=++s->generation;g[29]=READER_TITLE;
 U title=0;while(title<63&&((B*)g)[READER_TITLE+title])title++;g[30]=title;
 U selected=s->page;
 *control(a,784)=selected;*control(a,788)=1;*control(a,796)=0;*control(a,880)=s->lines[selected*s->rows].position;s->selected=s->target;s->positions[s->selected]=minimum(s->source_position,b->text_bytes);s->dirty=1;s->save_delay=30;s->stage=IDLE;acknowledge(a);return 0;
}
__attribute__((section(".text.iz_app"))) U iz_app(IzNativeApp*a,U event){
 if(!a||a->magic!=0x414e5a49||a->version!=2||a->bytes!=sizeof(IzNativeApp2)||!a->data||!a->controls||a->controls_bytes<READER_GPU+1024||!a->read_file||!a->write_file||a->data_bytes<64)return FAIL;
 ReaderConfig*p=a->data;if(p->state_offset>a->data_bytes||sizeof(State)>a->data_bytes-p->state_offset)return FAIL;State*s=(void*)((B*)p+p->state_offset);
 if(event==IZ_NATIVE_STOP){if(s->magic!=READER_STATE||s->stopped)return 0;if(s->stage==IDLE&&s->selected<p->books&&*control(a,788)==1){s->positions[s->selected]=minimum(*control(a,880),books(p)[s->selected].text_bytes);s->dirty=1;}(void)save_state(a,p,s);s->stopped=1;return 0;}
 if(event==IZ_NATIVE_INIT){
  if(s->magic||!validate(a,p))return FAIL;s->magic=READER_STATE;s->selected=READER_NONE;s->target=p->initial_open?p->initial_book:READER_NONE;copy(s->prefs,p->prefs,28);for(U i=0;i<p->books;i++)s->positions[i]=books(p)[i].position;
  restore(a,p,s);U*g=gpu(a);g[13]=p->books;g[14]=READER_LIBRARY_META/4;g[31]=READER_NONE;*control(a,788)=0;*control(a,812)=0;settings(a,s);
  for(U i=0;i<p->books;i++){U n=0;while(n<63&&books(p)[i].title[n])n++;copy((B*)g+READER_LIBRARY+i*64,books(p)[i].title,64);g[READER_LIBRARY_META/4+i*2]=READER_LIBRARY+i*64;g[READER_LIBRARY_META/4+i*2+1]=n;}
  if(s->target<p->books)begin_read(a,s,s->target);return 0;
 }
 if(event!=IZ_NATIVE_FRAME||s->magic!=READER_STATE||s->stopped)return FAIL;
 if(s->stage==READING){ReaderBook*b=books(p)+s->target;U n=minimum(65536,b->bytes-s->loaded),actual=0;U rc=a->read_file(a,assets(p)[b->asset].path,(Q)b->offset+s->loaded,s->payload+s->loaded,n,&actual);
  if(rc||actual!=n){failed(a,s);return 0;}s->crc=crc(s->crc,s->payload+s->loaded,n);s->loaded+=n;if(s->loaded==b->bytes){if(~s->crc!=b->crc||!dictionary(a,b,s)){failed(a,s);return 0;}begin_layout(a,p,s);}return 0;
 }
 if(s->stage==LAYOUT){ReaderBook*b=books(p)+s->target;if(s->cursor>b->text_bytes){if(!publish(a,b,s))return 0;}else{int rc=layout(a,b,s);if(!rc)failed(a,s);}return 0;}
 U seq=*control(a,804);if(*control(a,812)&&seq&&seq!=s->last_seq){
  s->last_seq=seq;U op=*control(a,800),arg=*control(a,808);*control(a,892)=0;
  if(op==1&&arg<p->books){if(s->selected<p->books)s->positions[s->selected]=minimum(*control(a,880),books(p)[s->selected].text_bytes);begin_read(a,s,arg);return 0;}
  if(op==2&&arg<7){const U counts[]={9,2,2,3,3,3,3};s->prefs[arg]=(s->prefs[arg]+counts[arg]+(*control(a,888)?1:counts[arg]-1))%counts[arg];s->dirty=1;s->save_delay=30;
   if(s->selected<p->books){s->positions[s->selected]=minimum(*control(a,880),books(p)[s->selected].text_bytes);s->target=s->selected;begin_layout(a,p,s);return 0;}settings(a,s);
  }else if(op==3&&s->selected<p->books){s->positions[s->selected]=minimum(arg,books(p)[s->selected].text_bytes);s->dirty=1;s->save_delay=30;}else *control(a,892)=1;
  acknowledge(a);
 }
 if(s->dirty){if(s->save_delay)s->save_delay--;else if(!save_state(a,p,s))s->save_delay=300;}return 0;
}
