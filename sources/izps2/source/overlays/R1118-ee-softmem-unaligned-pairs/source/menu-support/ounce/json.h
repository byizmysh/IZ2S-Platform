/* Small bounded JSON reader for package manifests. No allocations or execution. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
typedef struct {int start,end,next,count;char kind;} JNode;
typedef struct {const char*s;int at,size,count;JNode nodes[4096];} Json;
static void jspace(Json*j){while(j->at<j->size&&strchr(" \t\r\n",j->s[j->at]))j->at++;}
static int jvalue(Json*j,int depth){
 jspace(j);if(depth>24||j->at>=j->size||j->count==4096)return -1;
 int n=j->count++;JNode*t=j->nodes+n;*t=(JNode){.start=j->at,.kind=j->s[j->at]};char c=j->s[j->at++];
 if(c=='{'||c=='['){jspace(j);if(j->at<j->size&&j->s[j->at]==(c=='{'?'}':']'))j->at++;
 else for(;;){if(c=='{'){jspace(j);if(j->at>=j->size||j->s[j->at]!='"'||jvalue(j,depth+1)<0)return -1;jspace(j);if(j->at>=j->size||j->s[j->at++]!=':')return -1;}
 if(jvalue(j,depth+1)<0)return -1;t->count++;jspace(j);if(j->at>=j->size)return -1;char end=j->s[j->at++];if(end==(c=='{'?'}':']'))break;if(end!=',')return -1;}}
 else if(c=='"'){for(;;){if(j->at>=j->size)return -1;unsigned x=(unsigned char)j->s[j->at++];if(x=='"')break;if(x<32)return -1;if(x=='\\'){if(j->at>=j->size)return -1;char esc=j->s[j->at++];if(esc=='u'){for(int i=0;i<4;i++)if(j->at>=j->size||!isxdigit((unsigned char)j->s[j->at++]))return -1;}else if(!strchr("\"\\/bfnrt",esc))return -1;}}}
 else {int start=j->at-1;while(j->at<j->size&&!strchr(" \t\r\n,]}",j->s[j->at]))j->at++;int len=j->at-start;
 if(!((len==4&&!memcmp(j->s+start,"true",4))||(len==5&&!memcmp(j->s+start,"false",5))||(len==4&&!memcmp(j->s+start,"null",4)))){int p=start;if(j->s[p]=='-')p++;if(p>=j->at)return -1;if(j->s[p]=='0')p++;else{if(j->s[p]<'1'||j->s[p]>'9')return -1;while(p<j->at&&isdigit((unsigned char)j->s[p]))p++;}if(p<j->at&&j->s[p]=='.'){p++;int b=p;while(p<j->at&&isdigit((unsigned char)j->s[p]))p++;if(p==b)return -1;}if(p<j->at&&(j->s[p]=='e'||j->s[p]=='E')){p++;if(p<j->at&&(j->s[p]=='+'||j->s[p]=='-'))p++;int b=p;while(p<j->at&&isdigit((unsigned char)j->s[p]))p++;if(p==b)return -1;}if(p!=j->at)return -1;}}
 t->end=j->at;t->next=j->count;return n;
}
static int jparse(Json*j,const char*s,int size){memset(j,0,sizeof *j);j->s=s;j->size=size;if(!size||memchr(s,0,size)||jvalue(j,0)!=0)return -1;jspace(j);if(j->at!=size||j->nodes[0].kind!='{')return -1;for(int i=0;i<j->count;i++)if(j->nodes[i].kind=='{'){for(int a=i+1;a<j->nodes[i].next;a=j->nodes[a+1].next){JNode*x=j->nodes+a;if(memchr(s+x->start,'\\',x->end-x->start))return -1;for(int b=j->nodes[a+1].next;b<j->nodes[i].next;b=j->nodes[b+1].next){JNode*y=j->nodes+b;if(x->end-x->start==y->end-y->start&&!memcmp(s+x->start,s+y->start,x->end-x->start))return -1;}}}return 0;}
/* Paths and contract keys use literal UTF-8. Escaped spellings are rejected
 * instead of admitting duplicate keys through ambiguous normalization. */
static int jstr(Json*j,int n,char*out,size_t cap){if(n<0||j->nodes[n].kind!='"')return -1;JNode*t=j->nodes+n;size_t len=(size_t)(t->end-t->start-2);if(len>=cap||memchr(j->s+t->start+1,'\\',len))return -1;memcpy(out,j->s+t->start+1,len);out[len]=0;return 0;}
static uint32_t jhex4(const char *value) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char digit = (unsigned char)value[i];
        if (digit >= '0' && digit <= '9') digit -= '0';
        else digit = (digit | 32) - 'a' + 10;
        result = (result << 4) | digit;
    }
    return result;
}

/* Decode display text from a token already validated by jvalue. */
static int jtext(Json *j, int n, char *out, size_t cap) {
    if (n < 0 || j->nodes[n].kind != '"' || !cap) return -1;
    const JNode *token = &j->nodes[n];
    const char *cursor = j->s + token->start + 1;
    const char *end = j->s + token->end - 1;
    size_t length = 0;
    while (cursor < end) {
        unsigned char byte = (unsigned char)*cursor++;
        if (byte != '\\' || *cursor != 'u') {
            if (byte == '\\') {
                byte = (unsigned char)*cursor++;
                if (byte != '"' && byte != '/' && byte != '\\') return -1;
            }
            if (length == cap - 1) return -1;
            out[length++] = (char)byte;
            continue;
        }

        uint32_t codepoint = jhex4(++cursor);
        cursor += 4;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (end - cursor < 6 || cursor[0] != '\\' || cursor[1] != 'u') return -1;
            const uint32_t low = jhex4(cursor + 2);
            if (low < 0xdc00 || low > 0xdfff) return -1;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            cursor += 6;
        } else if (codepoint < 0x20 || (codepoint >= 0xdc00 && codepoint <= 0xdfff)) {
            return -1;
        }

        const unsigned count = codepoint < 0x80 ? 1 : codepoint < 0x800 ? 2 : codepoint < 0x10000 ? 3 : 4;
        static const unsigned char prefix[] = {0, 0, 0xc0, 0xe0, 0xf0};
        if (count > cap - 1 - length) return -1;
        for (unsigned i = count - 1; i > 0; --i) {
            out[length + i] = (char)(0x80 | (codepoint & 0x3f));
            codepoint >>= 6;
        }
        out[length] = (char)(prefix[count] | codepoint);
        length += count;
    }
    out[length] = '\0';
    return 0;
}

static int jeq(Json*j,int n,const char*s){char value[256];return !jstr(j,n,value,sizeof value)&&!strcmp(value,s);}
static int jkey(Json*j,int n,const char*key){if(n<0||j->nodes[n].kind!='{')return -1;int found=-1;for(int p=n+1;p<j->nodes[n].next;){int v=p+1;if(jeq(j,p,key)){if(found>=0)return -2;found=v;}p=j->nodes[v].next;}return found;}
static int ju32(Json*j,int n,uint32_t*out){if(n<0)return -1;JNode*t=j->nodes+n;uint64_t v=0;if(t->end==t->start)return -1;for(int i=t->start;i<t->end;i++){if(j->s[i]<'0'||j->s[i]>'9')return -1;v=v*10+(j->s[i]-'0');if(v>UINT32_MAX)return -1;}*out=(uint32_t)v;return 0;}
static int jempty(Json*j,int n){return n<0||(j->nodes[n].kind=='['&&j->nodes[n].count==0);}
