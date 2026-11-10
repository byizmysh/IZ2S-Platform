'use strict';
async function create(rpc){
  if(rpc.nativeABI!=='civswitch')throw Error('Civ7 renderer requires the CivSwitch native transport');
  return {call:async(address,args=[])=>{
    if(args.length>6)throw Error('At most six native arguments');
    return (await rpc.callAddress(address,args)).result;
  }};
}
module.exports={create};
