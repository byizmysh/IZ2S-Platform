const net=require('node:net');
function validate(value){value.switchToken=String(value.switchToken||'').replace(/[\s-]/g,'').toLowerCase();const issues=[];if(!net.isIPv4(value.switchAddress||''))issues.push('Enter the Wi-Fi IPv4 address shown on Switch 1.');if(!/^[a-f0-9]{32}$/.test(value.switchToken||''))issues.push('Install the Switch 1 SD package from 0.3.1 to pair automatically.');const port=Number(value.switchPort||24872);if(!Number.isInteger(port)||port<1024||port>65535)issues.push('Switch keyboard port must be 1024..65535.');return issues;}
module.exports={validate};
