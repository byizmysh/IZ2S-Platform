'use strict';
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const test = require('node:test');
const apps = require('../../core/packages/repository-apps.cjs');

test('App Store uses only registered release assets and GitHub asset redirects', async () => {
  let requests = 0;
  const badRedirect = async url => {
    requests++;
    assert.equal(url.href, apps.release('izpsx').asset);
    return new Response(null, {status:302,headers:{location:'https://example.com/asset.iza'}});
  };
  assert.equal(await apps.download('vacootube',{request:badRedirect}),null);
  assert.equal(await apps.download('constructor',{request:badRedirect}),null);
  assert.equal(requests,0);
  await assert.rejects(apps.download('izpsx',{request:badRedirect}), /unexpected download address/);
  assert.equal(requests,1);
});

test('App Store rejects a same-size replacement with the wrong hash', async () => {
  const row=apps.release('blitbun');
  const bytes=Buffer.alloc(row.bytes);
  assert.notEqual(createHash('sha256').update(bytes).digest('hex'),row.sha256);
  const request=async url=>url.hostname==='github.com'
    ? new Response(null,{status:302,headers:{location:'https://release-assets.githubusercontent.com/example'}})
    : new Response(bytes);
  await assert.rejects(apps.download('blitbun',{request}),/SHA-256 check/);
});
