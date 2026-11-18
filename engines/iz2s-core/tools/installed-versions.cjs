'use strict';

const {createHash} = require('node:crypto');
const official = require('./official-apps.json');

function identity(bytes){
  return {bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};
}

function label(row,digest,receipts=[]){
  const candidate=[...receipts,...official].find(record=>record.id===row.id&&
    record.bytes===row.bytes&&record.sha256===digest);
  return {...row,sha256:digest,versionVerified:!!candidate,
    version:candidate?.version,sourceSha256:candidate?.sourceSha256};
}

module.exports={identity,label,official};
