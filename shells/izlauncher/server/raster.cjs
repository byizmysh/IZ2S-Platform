const path=require('node:path');
async function raster(catalog,directory,label='focus-catalog'){
 return require('../../../core/services/app-catalog.cjs').raster(catalog,directory,label,{postprocess:[path.join(__dirname,'hero-text.py')]});
}
module.exports={raster};
