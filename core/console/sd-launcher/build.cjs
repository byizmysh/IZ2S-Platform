'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),{execFileSync}=require('node:child_process');
const ROOT=path.resolve(__dirname,'../../..'),N=path.join(__dirname,'native'),OUT=path.join(N,'build');
async function main(){
 await fs.mkdir(OUT,{recursive:true});
 const sensors=require('../host/console-sensors.cjs'),inputLayout=require('../host/layout.cjs');
 if(sensors.BYTES!==64)throw Error('Update native sensor snapshot copy for changed SDK layout');
 await fs.writeFile(path.join(N,'host-input.h'),'/* Generated from the shared SDK input snapshot ABI. */\n#define IZ_CONSOLE_SENSOR_AT '+(inputLayout.OFFSET+sensors.OFFSET)+'u\n#define IZ_CONSOLE_SENSOR_BYTES '+sensors.BYTES+'u\n#define IZ_CONSOLE_SENSOR_MAGIC '+sensors.MAGIC+'u\n');
 // Match the exact launcher-owned executor, never an app's self-reported flag.
 await require('../../../tools/sd-launcher/build-instructions.cjs').main();
 const instructionImage=await fs.readFile(path.join(__dirname,'instructions/runtime.bin'));
 await fs.writeFile(path.join(N,'checkpoint-runtime.inc'),'/* Generated from the verified instruction executor. */\nstatic const u8 iz_checkpoint_instruction_image[]={\n'+Array.from(instructionImage,(b,i)=>(i%24===0?'\n':'')+b).join(',')+'\n};\n');
 const sources=['entry.S','memory.c','filesystem.c','native-loader.c','library.c','launcher.c','../relocatable/loader.c','../relocatable/rebase.c','../relocatable/address.c','../relocatable/system.S'];
 const objects=[];
 for(const name of sources){const object=path.join(OUT,path.basename(name)+'.o');const flags=name.endsWith('.S')?['-DIZ_STACK=944','-DIZ_STACK_BYTES=952']:['-std=c11','-Os','-ffreestanding','-fno-builtin','-fPIC','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-jump-tables','-mbranch-protection=bti','-mgeneral-regs-only','-mno-outline-atomics','-Wall','-Wextra','-Werror'];execFileSync('clang',['--target=aarch64-none-elf',...flags,'-c',path.join(N,name),'-o',object],{cwd:ROOT,stdio:'inherit'});require('../native/pic.cjs').verifyObject(object);objects.push(object);}
 const elf=path.join(OUT,'launcher.elf'),binary=path.join(OUT,'launcher.bin');
 execFileSync('ld.lld',['-T',path.join(N,'layout.ld'),'--no-undefined','--build-id=none',...objects,'-o',elf],{stdio:'inherit'});
 const unresolved=execFileSync('llvm-nm',['--undefined-only',elf],{encoding:'utf8'}).trim();if(unresolved)throw Error(unresolved);
 const relocations=execFileSync('llvm-readelf',['-r',elf],{encoding:'utf8'});if(!relocations.includes('There are no relocations'))throw Error('Native binary requires runtime relocations');
 execFileSync('llvm-objcopy',['-O','binary',elf,binary]);
 // Generate the host packing schema from the C compiler's actual ABI.
 const fields=['magic','version','stop','state','error','poisoned','active','count','frames','transitions','read_bytes','read_calls','opened','closed','probe','probe_result','rescan','scan_generation','functions','device','queue','swapchain','acquired','command_pool','controls_buffer','controls','controls_bytes','menu_layout','menu_descriptor','image_count','images','views','semaphores','menu_commands','snapshot','scratch','arena','arena_bytes','filesystem','memory_type','tick_hz','rejected','last_app_error','last_id','probe_nonce','probe_crc','stack','stack_bytes','ro_handle','native_status','pid','ro_pid','native_code_reads','native_launches'];
 fields.push('recovery_wake','catalog_revision','catalog_count','catalog','audio_functions','native_data','native_data_bytes','load_stage','staging_bytes');
 fields.push('library_revision','request_revision','library_staging','library_capacity','library_offer','library_applied','library_error','library_count','library','request_sequence','request_head','request_count','request_ack','request_ack_error','request_error','picker_observed','requests');
 fields.push('host_hold_request','host_hold_ready','host_hold_release','auto_scan_signature','auto_scan_error','remove_sequence','remove_error','removed_id','parent_snapshot');
 fields.push('ram_base','ram_bytes','ram_files','ram_read_bytes','ram_file_count','ram_only','ram_app_id');
 fields.push('host_mode','host_count','host_revision','host_applied','host_catalog','catalog_source','host_display_request','host_display_ready','host_display_release','host_launch','host_remove');
 fields.push('host_install_index','host_install_target','host_install_sequence','host_install_ack');
 fields.push('host_display_claim','host_display_rejected','host_display_observed');
 fields.push('sdk','worker_functions','graphics_vulkan','graphics_instance','graphics_physical','graphics_memory_properties','checkpoint_enabled','checkpoint_state','checkpoint_error','checkpoint_kind','checkpoint_bank','checkpoint_generation','checkpoint_logical','checkpoint_stored','checkpoint_time_bias','checkpoint_progress','owner');
 fields.splice(fields.indexOf('owner'),0,'supervisor_base','supervisor_bytes','host_frames_offer','host_frames_ack','host_frames_active','host_frames_paused','host_frames_returned','host_frames_reason','host_frames_error','host_frames_cancel','host_frames_plan');
 const schema=path.join(OUT,'schema.c');await fs.writeFile(schema,'#include <stdio.h>\n#include "../abi.h"\nint main(void){printf("{\\\"bytes\\\":%zu",sizeof(IzContext));\n'+fields.map(f=>'printf(",\\\"'+f+'\\\":%zu",offsetof(IzContext,'+f+'));').join('\n')+'\nputs("}");return 0;}\n');
 execFileSync('cc',['-std=c11',schema,'-o',path.join(OUT,'schema')]);const layout=JSON.parse(execFileSync(path.join(OUT,'schema'),[],{encoding:'utf8'}));
 layout.status_bytes=layout.owner;layout.owner_bytes=layout.bytes-layout.owner;
 if(layout.bytes>12288||layout.status_bytes<7848||layout.owner_bytes<1)throw Error('Invalid persistent owner layout');
 if(layout.stack!==944||layout.stack_bytes!==952)throw Error('Update entry.S stack offsets to match the context ABI');
 const text=await fs.readFile(path.join(N,'abi.h'),'utf8'),enums=text.split('enum {\n F_SEND,')[1].split('F_COUNT')[0];
 const functions=['F_SEND',...enums.split(/[\s,]+/).filter(s=>/^F_[A-Z_]+$/.test(s))].map(s=>s.slice(2));
 const bytes=await fs.readFile(binary),sha=b=>require('node:crypto').createHash('sha256').update(b).digest('hex');
 const report={format:'iz-sd-launcher-native-v1',bytes:bytes.length,sha256:sha(bytes),layout,functions,sources:{}};
 for(const name of ['abi.h','native-app.h','directory.h','layout.ld','checkpoint.h','checkpoint-runtime.inc','host-frames.h','host-input.h','../../host/layout.cjs','../../host/console-sensors.cjs','../instructions/checkpoint.h','../instructions/runtime.c','../instructions/runtime.bin','../relocatable/loader.h','../relocatable/image.h','../relocatable/address.h',...sources])report.sources[name]=sha(await fs.readFile(path.join(N,name)));
 await fs.writeFile(path.join(OUT,'manifest.json'),JSON.stringify(report,null,2)+'\n');
 for(const name of [...objects,elf,schema,path.join(OUT,'schema')])await fs.rm(name,{force:true});
 console.log(JSON.stringify({bytes:bytes.length,sha256:report.sha256,contextBytes:layout.bytes}));return report;
}
module.exports={main,OUT};if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});
