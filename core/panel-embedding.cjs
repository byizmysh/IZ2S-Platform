'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
async function origin(root) {
  try {return require('./workspace.cjs').localUrl(JSON.parse(await fs.readFile(path.join(root,'data/state/window.json'),'utf8')).url).origin;}
  catch {return null;}
}
function allow(server, parent) {
  if (!parent) return;
  parent = require('./workspace.cjs').localUrl(parent).origin;
  // Preserve a module's other policies. Only this exact local panel may frame
  // its interface; API authentication and Origin checks remain the module's.
  server.prependListener('request', (request,response) => {
    const writeHead = response.writeHead;
    response.writeHead = function(code, message, headers) {
      if (typeof message !== 'string') {headers = message; message = undefined;}
      const pairs = Array.isArray(headers) ? headers.reduce((rows,value,i) => i%2 ? rows : [...rows,[value,headers[i+1]]],[]) : Object.entries(headers || {});
      let policy = this.getHeader('content-security-policy') || '';
      for (const [key,value] of pairs) if (key.toLowerCase() === 'content-security-policy') policy = value;
      const rest = Object.fromEntries(pairs.filter(([key]) => !['content-security-policy','x-frame-options'].includes(key.toLowerCase())));
      this.removeHeader('x-frame-options');
      this.setHeader('content-security-policy', String(policy).replace(/(?:^|;)\s*frame-ancestors\s+[^;]*/gi,'') + "; frame-ancestors 'self' " + parent);
      return message === undefined ? writeHead.call(this,code,rest) : writeHead.call(this,code,message,rest);
    };
  });
}
module.exports = {origin, allow};
