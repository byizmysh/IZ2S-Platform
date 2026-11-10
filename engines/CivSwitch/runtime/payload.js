// Evaluated directly inside the existing CivSwitch loader, retaining its
// qualified primitives. No SmileBASIC offsets, filesystem calls or code writes.
if (!window.__iz2sCivRuntime || window.__iz2sCivRuntime.revision !== 6 && window.__iz2sCivRuntime.dispatch({command:'info'}).allocatedBytes === 0) {
  window.__iz2sCivRuntime = (() => {
    const allocations = new Map();
    const limit = 32 * 1024 * 1024;
    let allocated = 0;
    function module(name, address) {
      const offset = Number(read32(address + 4n));
      if (offset <= 0 || offset > 0x10000000) throw Error('Invalid module header');
      const mod = address + BigInt(offset);
      if (read32(mod) !== 0x30444f4dn) throw Error('Missing MOD0 header');
      const bssEnd = Number(BigInt.asIntN(32, read32(mod + 12n))) + offset;
      if (bssEnd < offset || bssEnd > 0x10000000) throw Error('Invalid module size');
      return {name, base:String(address), size:bssEnd, codeSize:offset & ~4095};
    }
    const modules = [module('main', main_base), module('sdk', sdk_base)];
    const contains = (base, bytes, address, size) => address >= base && address+BigInt(size) <= base+BigInt(bytes);
    function owned(address, size) {
      for (const [base, bytes] of allocations) if (contains(base, bytes, address, size)) return true;
      return window.__iz2sCivRuntime.native?.owned(address, size) || false;
    }
    function readable(address, size) {
      return owned(address, size) || modules.some(m => contains(BigInt(m.base), m.size, address, size)) ||
        window.__iz2sCivRuntime.native?.readable(address, size);
    }
    function integer(value, max) {
      if (!Number.isSafeInteger(value) || value < 0 || value > max) throw Error('Invalid memory length');
      return value;
    }
    return {revision:6, dispatch(request) {
      if (request.command === 'quiet') {window.__iz2sCivRuntime.quiet = request.enabled === true; return true;}
      if (request.command === 'notice') {
        if(typeof request.text!=='string'||request.text.length>512||/[^\x20-\x7e]/.test(request.text))throw Error('Invalid console progress message');
        return log(request.text).then(()=>true);
      }
      if (['lookup','resolve-import','call','query','register-module','release-native'].includes(request.command)) {
        if (!window.__iz2sCivRuntime.native) throw Error('CivSwitch native adapter is not installed');
        return window.__iz2sCivRuntime.native.dispatch(request);
      }
      if (request.command === 'info') return {abi:1, engine:'civswitch', filesystem:false,
        modules, allocatedBytes:allocated, capabilities:['module-read','owned-memory']};
      if (request.command === 'allocate') {
        const size = integer(request.size, limit);
        if (!size || allocated+size > limit) throw Error('CivSwitch allocation limit exceeded');
        const address = malloc(size);
        if (!address) throw Error('CivSwitch allocation failed');
        allocations.set(address, size); allocated += size;
        return String(address);
      }
      const address = BigInt(request.address);
      if (request.command === 'free') {
        if (!allocations.has(address)) throw Error('Allocation is not owned by this runtime');
        free(address); allocated -= allocations.get(address); allocations.delete(address); return true;
      }
      if (request.command === 'read') {
        const size = integer(request.size, 32768);
        if (!readable(address, size)) throw Error('Read is outside an inspected module or owned allocation');
        let hex = '';
        let offset=0;
        for(;offset+8<=size;offset+=8)hex+=read64(address+BigInt(offset)).toString(16).padStart(16,'0').match(/../g).reverse().join('');
        for (; offset < size; offset++) hex += Number(read8(address+BigInt(offset))).toString(16).padStart(2,'0');
        return hex;
      }
      if(request.command==='validate-owned'){
        const address=BigInt(request.address),size=request.size;
        if(!Number.isSafeInteger(size)||size<1||size>32*1024*1024||!owned(address,size))throw Error('Range is outside an owned allocation');
        return true;
      }
      if (request.command === 'write') {
        const hex = request.hex;
        if (typeof hex !== 'string' || hex.length > 65536 || hex.length%2 || !/^[0-9a-f]*$/.test(hex)) throw Error('Invalid memory bytes');
        if (!owned(address, hex.length/2)) throw Error('Writes are limited to this runtime allocations');
        let offset=0;
        for(;offset+8<=hex.length/2;offset+=8)write64(address+BigInt(offset),BigInt('0x'+hex.slice(offset*2,offset*2+16).match(/../g).reverse().join('')));
        for (; offset < hex.length/2; offset++) write8(address+BigInt(offset), BigInt(parseInt(hex.slice(offset*2,offset*2+2),16)));
        return true;
      }
      throw Error('Unsupported CivSwitch runtime command');
    }};
  })();
}
if (!window.__iz2sCivRuntime.loggerInstalled) {
  const loaderLog = window.log;
  window.log = async message => {
    if (window.__iz2sCivRuntime.quiet && /^(?:Executing payload\.\.\.|Executed successfully|Awaiting payload at )/.test(String(message))) return Promise.resolve();
    // A separate native layer can stop the game's DOM animation frames.
    // Never let its screen logger suspend the TCP loader indefinitely.
    // Unicode code points also keep each native log write below 2 KiB.
    const points=Array.from(String(message));
    for(let offset=0;offset<Math.max(1,points.length);offset+=256){
      let timer;
      try{await Promise.race([loaderLog(points.slice(offset,offset+256).join('')),
        new Promise(resolve=>{timer=setTimeout(resolve,75);})]);}
      finally{clearTimeout(timer);}
    }
  };
  window.__iz2sCivRuntime.loggerInstalled = true;
}
if(window.__iz2sCivRuntime.quiet===undefined)window.__iz2sCivRuntime.quiet=false;
if(typeof window.__iz2sCivRuntime.dispatch==='function'&&!window.__iz2sCivRuntime.transferMonitorInstalled){
  const dispatch=window.__iz2sCivRuntime.dispatch;
  window.__iz2sCivRuntime.dispatch=request=>{
    const result=dispatch(request);
    if(request.command==='write'&&request.hex?.length){
      const bytes=request.hex.length/2,previous=window.__iz2sCivRuntime.transfer;
      window.__iz2sCivRuntime.transfer={bytes:(previous?.bytes||0)+bytes,lastBytes:bytes,address:request.address,at:Date.now()};
    }
    if(request.command==='info')return {...result,transfer:window.__iz2sCivRuntime.transfer||null};
    return result;
  };
  window.__iz2sCivRuntime.transferMonitorInstalled=true;
}
// The stock logger also paints every response on the game screen. Use its
// existing socket and buffer for bounded ASCII protocol frames instead. Send
// partial writes to completion and never exceed the 2048-byte native buffer.
if (!window.__iz2sCivRuntime.emit || window.__iz2sCivRuntime.emitterRevision !== 2) {
  window.__iz2sCivRuntime.emit = text => {
    if (typeof text !== 'string' || text.length > 256*1024 || /[^\x00-\x7f]/.test(text)) throw Error('Invalid wire response');
    for (let offset = 0; offset < text.length; offset += 1024) {
      const part = text.slice(offset, offset+1024);
      let at=0;
      for(;at+8<=part.length;at+=8){
        let word=0n;for(let i=0;i<8;i++)word|=BigInt(part.charCodeAt(at+i))<<BigInt(i*8);
        write64(line_buf+BigInt(at),word);
      }
      for(;at<part.length;at++)write8(line_buf+BigInt(at),BigInt(part.charCodeAt(at)));
      let sent = 0;
      while (sent < part.length) {
        const count = Number(send(_log_socket_fd, line_buf+BigInt(sent), part.length-sent, 0));
        if (count <= 0 || count > part.length-sent) throw Error('CivSwitch wire response failed');
        sent += count;
      }
    }
  };
  window.__iz2sCivRuntime.emitterRevision=2;
}
// Explicit large GPU profiles use the same driver-owned mappings. This check
// changes no memory and never treats a queried foreign range as owned.
if(!window.__iz2sCivRuntime.largeOwnedValidation){
  const previous=window.__iz2sCivRuntime.dispatch;
  window.__iz2sCivRuntime.dispatch=request=>{
    if(request.command==='validate-owned'&&request.size>32*1024*1024){
      if(!Number.isSafeInteger(request.size)||request.size>128*1024*1024||
        !window.__iz2sCivRuntime.native?.owned(BigInt(request.address),request.size))throw Error('Range is outside an owned allocation');
      return true;
    }
    return previous(request);
  };
  window.__iz2sCivRuntime.largeOwnedValidation=true;
}
// Decode ASCII already received into the loader's existing native buffer.
// No address is extracted from a newly allocated JavaScript object.
if(window.__iz2sCivRuntime.bulkWriteInstalled!==4){
  const previous=window.__iz2sCivRuntime.dispatch;
  window.__iz2sCivRuntime.dispatch=request=>{
    if(request.command==='bulk-write'){
      const size=request.size,sourceSize=request.sourceSize,offset=request.offset,encoded=4*Math.ceil(sourceSize/3);
      if(!Number.isInteger(size)||size<1||size>4*1024*1024||!Number.isInteger(sourceSize)||sourceSize<1||sourceSize>360*1024||!['raw','zlib'].includes(request.encoding)||request.encoding==='raw'&&sourceSize!==size||offset!==8192||
        request.received!==offset+encoded+2||request.received>=500*1024||typeof payload_buf!=='bigint')throw Error('Invalid native bulk source');
      previous({command:'validate-owned',address:request.address,size});
      const name='_ZN2nn4util6Base6416FromBase64StringEPmPvmPKcmNS1_4ModeE',native=window.__iz2sCivRuntime.native;
      native.dispatch({command:'lookup',name,module:'sdk'});
      const output=malloc(8),temporary=request.encoding==='zlib'?malloc(sourceSize):0n,work=request.encoding==='zlib'?malloc(1024*1024):0n;
      if(!output||request.encoding==='zlib'&&(!temporary||!work))throw Error('Bulk metadata allocation failed');
      try{
        write64(output,0n);
        const result=BigInt(native.dispatch({command:'call',name,module:'sdk',args:[String(output),String(temporary||BigInt(request.address)),String(sourceSize),String(payload_buf+BigInt(offset)),String(encoded),'0']}));
        if(result!==0n||read64(output)!==BigInt(sourceSize))throw Error('Native base64 decode did not confirm all bytes: result='+result+', bytes='+read64(output));
        if(request.encoding==='zlib'){
          const decompress='_ZN2nn4util14DecompressZlibEPvmPKvmS1_m';native.dispatch({command:'lookup',name:decompress,module:'sdk'});
          if(BigInt(native.dispatch({command:'call',name:decompress,module:'sdk',args:[request.address,String(size),String(temporary),String(sourceSize),String(work),String(1024*1024)]}))!==1n)throw Error('Native decompression failed');
        }
        return {written:size};
      }finally{if(work)free(work);if(temporary)free(temporary);free(output);}
    }
    return previous(request);
  };
  window.__iz2sCivRuntime.bulkWriteInstalled=4;
}
