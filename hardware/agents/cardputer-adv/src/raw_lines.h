#pragma once
#include <cstring>
#include <cstddef>
// Select directly inside the parsed SD document. No full-source String copy.
inline bool selectRawLine(const char *text,int index,const char *&begin,size_t &length){
  if(!text||index<0)return false;
  begin=text;
  for(int i=0;i<index;i++){begin=strchr(begin,'\n');if(!begin)return false;begin++;}
  const char *end=strchr(begin,'\n');if(!end)end=begin+strlen(begin);
  if(end>begin&&end[-1]=='\r')end--;
  length=end-begin;
  if(!length||length>253)return false;
  for(size_t i=0;i<length;i++)if(static_cast<unsigned char>(begin[i])<32||static_cast<unsigned char>(begin[i])>126)return false;
  return true;
}
