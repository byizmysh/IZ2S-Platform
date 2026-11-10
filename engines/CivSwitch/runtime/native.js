// Named exports are resolved in the live Civ7 module's SysV hash table. SB4
// SDK offsets and instruction gadgets are never treated as Civ7 addresses.
if(window.__iz2sCivRuntime.native && window.__iz2sCivRuntime.native.revision!==5){
  if(window.__iz2sCivRuntime.native.hasResources)throw Error('Stop the active graphics runtime before upgrading its adapter');
  window.__iz2sCivRuntime.native.dispatch({command:'release-native'});
  delete window.__iz2sCivRuntime.native;
}
if (window.__iz2sCivRuntime.revision === 6 && !window.__iz2sCivRuntime.native) {
  window.__iz2sCivRuntime.native = (() => {
    const modules = window.__iz2sCivRuntime.dispatch({command:'info'}).modules;
    const bindings = new Map(), tables = new Map(), regions = new Map(),allocations=new Map(),mappedHandles=new Map();
    const hexAt = (address, size) => {
      let hex='';for(let i=0;i<size;i++)hex+=Number(read8(address+BigInt(i))).toString(16).padStart(2,'0');return hex;
    };
    function relative(module, offset, size) {
      if (!Number.isSafeInteger(offset) || offset < 0 || !Number.isSafeInteger(size) || size < 0 || offset+size>module.size)
        throw Error('CivSwitch symbol table escaped its module');
      return BigInt(module.base)+BigInt(offset);
    }
    function symbol(module, name, imported=false) {
      if (typeof name !== 'string' || name.length>512 || /[^\x20-\x7e]/.test(name)) throw Error('Invalid export name');
      let table=tables.get(module.name);
      if(!table){
        const base=BigInt(module.base),mod=Number(read32(base+4n)),dynamic=mod+Number(BigInt.asIntN(32,read32(base+BigInt(mod)+4n))),tags=new Map();
        for(let at=0;at<4096;at+=16){const p=relative(module,dynamic+at,16),tag=Number(read64(p));if(!tag)break;tags.set(tag,Number(read64(p+8n)));}
        if(tags.get(11)!==24)throw Error('Unsupported CivSwitch symbol layout');
        const hash=relative(module,tags.get(4),8),buckets=Number(read32(hash)),count=Number(read32(hash+4n));
        if(!buckets||buckets>200000||!count||count>200000||!tags.get(10)||tags.get(10)>0x800000)throw Error('Invalid CivSwitch symbol bounds');
        relative(module,tags.get(4),8+4*(buckets+count));
        table={hash,buckets,count,symbols:relative(module,tags.get(6),count*24),strings:relative(module,tags.get(5),tags.get(10)),stringSize:tags.get(10),
          relocations:[[tags.get(23),tags.get(2)],[tags.get(7),tags.get(8)]]};
        tables.set(module.name,table);
      }
      let hash=0;
      for(let i=0;i<name.length;i++){hash=((hash<<4)+name.charCodeAt(i))>>>0;const high=hash&0xf0000000;if(high)hash^=high>>>24;hash=(hash&~high)>>>0;}
      let index=Number(read32(table.hash+8n+BigInt((hash%table.buckets)*4)));
      for(let steps=0;index;steps++){
        if(index>=table.count||steps>=table.count)throw Error('Invalid CivSwitch hash chain');
        const p=table.symbols+BigInt(index*24),nameOffset=Number(read32(p)),offset=Number(read64(p+8n)),size=Number(read64(p+16n));
        if(nameOffset<table.stringSize){
          let actual='';for(let i=0;i<=name.length && nameOffset+i<table.stringSize;i++){const c=Number(read8(table.strings+BigInt(nameOffset+i)));if(!c)break;actual+=String.fromCharCode(c);}
          if(actual===name){
            if(imported){
              if(read16(p+6n))throw Error('Requested symbol is defined instead of imported');
              return {index,relocations:table.relocations};
            }
            if((Number(read8(p+4n))&15)!==2||!read16(p+6n)||offset<16||offset+size>module.codeSize)throw Error('Export is not a defined CivSwitch function: '+name);
            const address=relative(module,offset,Math.min(32,size));
            return {module:module.name,name,address:String(address),size,hex:hexAt(address,Math.min(32,size))};
          }
        }
        index=Number(read32(table.hash+8n+BigInt((table.buckets+index)*4)));
      }
      throw Error('CivSwitch export is missing: '+name);
    }
    let stack=0n, callNative=null;
    const stackBytes=0x240000,stackGuardOffset=0x1000,stackGuard='a7'.repeat(64);
    function caller(){
      if(callNative)return callNative;
      // The upstream ROP caller only reserves about 16 KiB below its pivot.
      // Driver compilation needs the same two MiB stack reserve as SB4's
      // worker. Keep this separate from V8's backing stores and loader stack.
      const source=call_rop.toString();
      // Inner locals are mangled by upstream compression. The outer pivot
      // binding remains named because the loader uses direct eval.
      if(!/\brop_chain_addr\b/.test(source))throw Error('Unsupported CivSwitch native caller');
      stack=malloc(stackBytes);if(!stack)throw Error('Native stack allocation failed');
      try{
        for(let i=0;i<64;i++)write8(stack+BigInt(stackGuardOffset+i),0xa7n);
        const rop_chain_addr=stack+0x210000n;
        callNative=eval('('+source+')');
        return callNative;
      }catch(error){free(stack);stack=0n;throw error;}
    }
    function invoke(binding,args){
      if(!Array.isArray(args)||args.length>6)throw Error('CivSwitch supports at most six integer/pointer arguments');
      const address=BigInt(binding.address);
      if(hexAt(address,binding.hex.length/2)!==binding.hex)throw Error('CivSwitch function changed: '+binding.name);
      const fn=caller();
      if(hexAt(stack+BigInt(stackGuardOffset),64)!==stackGuard)throw Error('Native stack guard changed; calls stopped');
      const result=fn(address,...args.map(value=>BigInt(value)));
      if(hexAt(stack+BigInt(stackGuardOffset),64)!==stackGuard)throw Error('Native stack exceeded its reserve; calls stopped');
      if(result===0n&&binding.name==='posix_memalign'){
        const base=read64(BigInt(args[0])),size=Number(args[2]);
        if(!base||!Number.isSafeInteger(size)||size<=0||size>0x20000000)throw Error('Invalid native allocation result');
        allocations.set(base,size);
      }
      if(result===0n&&binding.name==='vkMapMemory2'){
        const base=read64(BigInt(args[2])),size=Number(read64(BigInt(args[1])+40n));
        if(!base||!Number.isSafeInteger(size)||size<=0||size>0x20000000)throw Error('Invalid mapped GPU allocation');
        allocations.set(base,size);
        mappedHandles.set(read64(BigInt(args[1])+24n),base);
      }
      if(binding.name==='free')allocations.delete(BigInt(args[0]));
      if(binding.name==='vkFreeMemory'){
        const handle=BigInt(args[1]),base=mappedHandles.get(handle);
        if(base!==undefined){allocations.delete(base);mappedHandles.delete(handle);}
      }
      return result;
    }
    let queryBinding;
    function query(address){
      if(address<0n||address>=0x8000000000n)throw Error('Invalid console virtual address');
      if(!queryBinding){
        const sdk=modules.find(m=>m.name==='sdk'),parser=symbol(sdk,'_ZN2nn4diag6detail17MemoryBlockParser8MoveNextEv');
        const base=BigInt(parser.address),matches=[];
        const expected='e10f1ff8c10000d4e20340f9410000b9ff430091c0035fd6';
        for(let offset=8;offset<Math.min(parser.size,512);offset+=4){
          const instruction=Number(read32(base+BigInt(offset)));
          if((instruction>>>26)!==0x25)continue;
          const displacement=(instruction<<6)>>4,target=base+BigInt(offset+displacement),moduleBase=BigInt(sdk.base);
          if(target<moduleBase||target+24n>moduleBase+BigInt(sdk.codeSize))continue;
          if(hexAt(target,24)===expected)matches.push(target);
        }
        if(matches.length!==1)throw Error('CivSwitch QueryMemory wrapper was not uniquely verified');
        queryBinding={name:'svcQueryMemory',address:String(matches[0]),hex:expected};
      }
      const output=malloc(80);if(!output)throw Error('Memory query allocation failed');
      try{
        const rc=invoke(queryBinding,[String(output),String(output+64n),String(address)]);
        if(rc!==0n)throw Error('CivSwitch QueryMemory failed: '+rc.toString(16));
        const base=read64(output),size=read64(output+8n),permissions=Number(read32(output+24n));
        if(size<=0n||base>address||base+size<=address)throw Error('Invalid console memory mapping');
        const result={address:String(base),size:String(size),type:Number(read32(output+16n)),attributes:Number(read32(output+20n)),permissions,
          ipcReferences:Number(read32(output+28n)),deviceReferences:Number(read32(output+32n))};
        if((permissions&1)&&regions.size<4096)regions.set(base,result);
        return result;
      }finally{free(output);}
    }
    return {
      revision:5,
      get hasResources(){return allocations.size>0;},
      owned:(address,size)=>[...allocations].some(([base,bytes])=>address>=base&&address+BigInt(size)<=base+BigInt(bytes)),
      readable:(address,size)=>[...regions.values()].some(r=>(r.permissions&1)&&address>=BigInt(r.address)&&address+BigInt(size)<=BigInt(r.address)+BigInt(r.size)),
      dispatch(request){
        if(request.command==='lookup'){
          const module=modules.find(m=>m.name===(request.module||'sdk'));if(!module)throw Error('Unknown CivSwitch module');
          const binding=symbol(module,request.name);bindings.set(module.name+':'+binding.name,binding);return binding;
        }
        if(request.command==='query')return query(BigInt(request.address));
        if(request.command==='resolve-import'){
          const module=modules.find(m=>m.name===(request.module||'main'));if(!module)throw Error('Unknown import module');
          const imported=symbol(module,request.name,true),slots=[];
          for(const [offset,size]of imported.relocations){
            if(offset===undefined||size===undefined||!size)continue;
            if(size%24||size>24*400000)throw Error('Invalid relocation table size');
            const table=relative(module,offset,size);
            for(let at=0;at<size;at+=24){
              const p=table+BigInt(at),info=read64(p+8n);
              if(Number(info>>32n)!==imported.index||![1025,1026].includes(Number(info&0xffffffffn)))continue;
              const slot=relative(module,Number(read64(p)),8),address=read64(slot);
              if(!address)throw Error('Unresolved graphics import');
              slots.push({slot:String(slot),address:String(address)});
            }
            if(slots.length)break;
          }
          if(slots.length!==1)throw Error('Imported function is not uniquely resolved: '+request.name);
          return {...slots[0],module:module.name,name:request.name};
        }
        if(request.command==='register-module'){
          if(request.name!=='graphics')throw Error('Only the graphics driver may be registered');
          const base=BigInt(request.address),region=regions.get(base);
          if(!region||region.permissions!==5||region.type!==3)throw Error('Graphics module needs a queried executable mapping');
          const offset=Number(read32(base+4n));
          if(offset<=0||offset>0x10000000)throw Error('Invalid graphics module header');
          const mod=base+BigInt(offset);
          if(![...regions.values()].some(r=>(r.permissions&1)&&mod>=BigInt(r.address)&&mod+16n<=BigInt(r.address)+BigInt(r.size))||read32(mod)!==0x30444f4dn)throw Error('Graphics MOD0 is not in a readable mapping');
          const size=offset+Number(BigInt.asIntN(32,read32(mod+12n)));
          if(size<offset||size>0x10000000)throw Error('Invalid graphics module size');
          const previous=modules.find(m=>m.name==='graphics');
          if(previous&&previous.base!==String(base)){
            if([...bindings.keys()].some(key=>key.startsWith('graphics:')))throw Error('Active graphics driver changed in this session');
            modules.splice(modules.indexOf(previous),1);tables.delete('graphics');
          }
          const module=previous?.base===String(base)?previous:{name:'graphics',base:String(base),size,codeSize:offset&~4095};
          if(!modules.includes(module))modules.push(module);return module;
        }
        if(request.command==='release-native'){
          if(stack&&hexAt(stack+BigInt(stackGuardOffset),64)!==stackGuard)throw Error('Uncertain native stack must be retained');
          if(stack)free(stack);stack=0n;callNative=null;return true;
        }
        if(request.command==='call'){
          const binding=bindings.get((request.module||'sdk')+':'+request.name);
          if(!binding)throw Error('Resolve the named CivSwitch function before calling it');
          return String(invoke(binding,request.args));
        }
        throw Error('Unknown native request');
      },
    };
  })();
}
