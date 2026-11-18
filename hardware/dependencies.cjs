// Runtime requirements for the precompiled distribution.
const path=require('node:path'),{spawnSync}=require('node:child_process'),root=require('../core/root.cjs');
function inspect({hostOnly=process.argv.includes('--host-only'),ssh=process.argv.includes('--ssh'),run=spawnSync,version=process.versions.node}={}){
 const rows=[],parts=version.split('.').map(Number);
 const check=(name,ready,fix)=>rows.push({name,ready:!!ready,fix});
 const runs=(program,args)=>{const r=run(program,args,{stdio:'ignore',timeout:15000,windowsHide:true});return !r.error&&r.status===0;};
 check('Node.js 20.20+ (installer uses 24)',parts[0]>20||(parts[0]===20&&parts[1]>=20),'Run the installer for this OS');
 check('Python + Pillow',runs(require('../core/platform/python.cjs').command(root),['-c',"from PIL import Image,ImageDraw,ImageFont,ImageOps;assert hasattr(ImageDraw.ImageDraw,'textbbox') and hasattr(ImageFont.FreeTypeFont,'getlength') and hasattr(ImageOps,'contain')"]),'Run the installer for your OS to prepare Python and Pillow');
 check('Bundled fonts',runs(require('../core/platform/python.cjs').command(root),['-c',"import sys;sys.path.insert(0,sys.argv[1]);from language.fonts import font;from PIL import ImageFont;ImageFont.truetype(font(),16)",root]),'Restore language/fonts');
 if(ssh)check('OpenSSH client',runs('ssh',['-V']),'Install the OpenSSH client for ARM input');
 const catalog=require('../core/extensions.cjs').catalog(root);
 check('Engine descriptors',catalog.engines.items.length>0&&catalog.engines.errors.length===0,'Restore bundled engine files');
 const required=require('../distribution/profile.json').shells;
 check('IZ2S console shell',catalog.shells.errors.length===0&&required.every(id=>catalog.shells.items.some(x=>x.id===id)),'Restore the bundled IZ2S console shell');
 for(const id of required){let failure;try{require('../core/session/runtime-check.cjs').verify(root,id);}catch(e){failure=e;}check('IZ2S runtime resources',!failure,failure?.message||'Restore IZ2S runtime resources');}
 return rows;
}
if(require.main===module){const rows=inspect();for(const row of rows)console.log((row.ready?'OK   ':'NEED ')+row.name+(row.ready?'':' - '+row.fix));if(rows.some(row=>!row.ready))process.exitCode=1;}
module.exports={inspect};
