function endpoint(value){
 const text=String(value||'').trim();
 let url;
 try{url=new URL(text.includes('://')?text:'http://'+text);}catch{throw Error('Enter the Cardputer IP address or local hostname, for example 192.168.1.50');}
 if(url.protocol!=='http:'||url.username||url.password||url.search||url.hash||!['','/'].includes(url.pathname))throw Error('Use the Cardputer IP address or local HTTP address without a path or credentials');
 return url.origin;
}
function validate(value){
 const requirements=[];
 if(value.cardputerEndpoint){
  value.cardputerEndpoint=endpoint(value.cardputerEndpoint);
 }else requirements.push('Set the Wi-Fi address shown on the Cardputer screen.');
 if(!/^[a-f0-9]{64}$/.test(value.cardputerToken||''))requirements.push('Prepare the Cardputer SD card package to pair the device.');
 if(!['off','on'].includes(value.cardputerCapsLock))requirements.push('Confirm Switch Caps Lock state.');
 return requirements;
}
module.exports={validate,endpoint};
