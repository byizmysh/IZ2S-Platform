/* Optional PC-only N-API accelerator. Console protocol and BC1 bytes stay intact. */
#include <node_api.h>
#include <libdeflate.h>
#include <stdlib.h>
#include <stdint.h>
typedef struct { struct libdeflate_compressor *compressors[13]; } Context;
static void cleanup(void *data) { Context *c=data; for(int i=0;i<13;i++)libdeflate_free_compressor(c->compressors[i]);free(c); }
static napi_value error(napi_env env,const char *message){napi_throw_error(env,NULL,message);return NULL;}
static napi_value crc(napi_env env,napi_callback_info info){size_t count=1,n;void *p;bool buffer;napi_value args[1],out;if(napi_get_cb_info(env,info,&count,args,NULL,NULL)!=napi_ok||count!=1||napi_is_buffer(env,args[0],&buffer)!=napi_ok||!buffer||napi_get_buffer_info(env,args[0],&p,&n)!=napi_ok||n>1036800)return error(env,"Invalid CRC buffer");if(napi_create_uint32(env,libdeflate_crc32(0,p,n),&out)!=napi_ok)return error(env,"CRC result allocation failed");return out;}
static napi_value deflate(napi_env env,napi_callback_info info){size_t count=2,n;void *p,*opaque;bool buffer;int32_t level;napi_value args[2],out;
 if(napi_get_cb_info(env,info,&count,args,NULL,&opaque)!=napi_ok||count!=2||napi_is_buffer(env,args[0],&buffer)!=napi_ok||!buffer||napi_get_buffer_info(env,args[0],&p,&n)!=napi_ok||n>32768||napi_get_value_int32(env,args[1],&level)!=napi_ok||(level!=1&&level!=3&&level!=6))return error(env,"Invalid compression block");
 Context *c=opaque;if(!c->compressors[level])c->compressors[level]=libdeflate_alloc_compressor(level);if(!c->compressors[level])return error(env,"Compressor allocation failed");
 size_t bound=libdeflate_deflate_compress_bound(c->compressors[level],n);void *bytes=malloc(bound);if(!bytes)return error(env,"Output allocation failed");size_t written=libdeflate_deflate_compress(c->compressors[level],p,n,bytes,bound);if(!written){free(bytes);return error(env,"Compression failed");}napi_status status=napi_create_buffer_copy(env,written,bytes,NULL,&out);free(bytes);if(status!=napi_ok)return error(env,"Output copy failed");return out;
}
static napi_value init(napi_env env,napi_value exports){Context *c=calloc(1,sizeof(*c));if(!c)return error(env,"Context allocation failed");napi_property_descriptor methods[]={{"crc32",NULL,crc,NULL,NULL,NULL,napi_default,c},{"deflate",NULL,deflate,NULL,NULL,NULL,napi_default,c}};if(napi_define_properties(env,exports,2,methods)!=napi_ok){cleanup(c);return error(env,"Native exports failed");}napi_add_env_cleanup_hook(env,cleanup,c);return exports;}
NAPI_MODULE(NODE_GYP_MODULE_NAME,init)
