// Runtime requirements are separate from developer-only shader build tools.
const path=require('node:path'),{spawnSync}=require('node:child_process'),root=require('../core/root.cjs');
function inspect({hostOnly=process.argv.includes('--host-only'),ssh=process.argv.includes('--ssh'),run=spawnSync,version=process.versions.node}={}){
 const rows=[],parts=version.split('.').map(Number);
 const check=(name,ready,fix)=>rows.push({name,ready:!!ready,fix});
 const runs=(program,args)=>{const r=run(program,args,{stdio:'ignore',timeout:15000});return !r.error&&r.status===0;};
 check('Node.js 20.20+ (installer uses 24)',parts[0]>20||(parts[0]===20&&parts[1]>=20),'Run the installer for this OS');
 check('Python + Pillow',runs(require('../core/platform/python.cjs').command(root),['-c',"from PIL import Image,ImageDraw,ImageFont,ImageOps;assert hasattr(ImageDraw.ImageDraw,'textbbox') and hasattr(ImageFont.FreeTypeFont,'getlength') and hasattr(ImageOps,'contain')"]),'Run the installer for your OS to prepare Python and Pillow');
 check('Bundled fonts',runs(require('../core/platform/python.cjs').command(root),['-c',"import sys;sys.path.insert(0,sys.argv[1]);from language.fonts import font;from PIL import ImageFont;ImageFont.truetype(font(),16)",root]),'Restore language/fonts');
 if(ssh)check('OpenSSH client',runs('ssh',['-V']),'Install the OpenSSH client for ARM input');
 if(!hostOnly){check('SPIR-V validator',runs('spirv-val',['--version']),'Install spirv-tools for shader builds');check('GLSL compiler',runs('glslangValidator',['--version']),'Install glslang-tools for shader builds');}
 const catalog=require('../core/extensions.cjs').catalog(root);
 check('Engine descriptors',catalog.engines.items.length>0&&catalog.engines.errors.length===0,'Restore bundled engine files');
 check('Console launchers',catalog.shells.items.some(x=>x.id==='izlauncher')&&catalog.shells.items.some(x=>x.id==='izpslauncher'),'Restore both console launchers');
 return rows;
}
if(require.main===module){const rows=inspect();for(const row of rows)console.log((row.ready?'OK   ':'NEED ')+row.name+(row.ready?'':' - '+row.fix));if(rows.some(row=>!row.ready))process.exitCode=1;}
module.exports={inspect};
