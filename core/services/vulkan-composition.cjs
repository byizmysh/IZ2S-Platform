// Negotiate ordinary Vulkan swapchain alpha composition only. This does not
// hide a window, establish BASIC input focus, or prove that an underlay exists.
const FLAGS=Object.freeze({OPAQUE:1,PRE_MULTIPLIED:2,POST_MULTIPLIED:4,INHERIT:8});

function parseCompositionRequest(value){
 const requested=value===undefined?'opaque':value;
 if(requested!=='opaque'&&requested!=='transparent')throw new TypeError('Invalid Vulkan composition request: expected "opaque" or "transparent"');
 return requested;
}

function readSupportedCompositeAlpha(capabilities){
 // VkSurfaceCapabilitiesKHR is 52 bytes; supportedCompositeAlpha is at 44.
 if(!Buffer.isBuffer(capabilities)||capabilities.length<52)throw new TypeError('Expected a complete 52-byte VkSurfaceCapabilitiesKHR buffer');
 return capabilities.readUInt32LE(44);
}

function selectCompositeAlpha(supportedMask,request){
 if(!Number.isInteger(supportedMask)||supportedMask<0||supportedMask>0xffffffff)throw new TypeError('Invalid Vulkan supportedCompositeAlpha mask: expected uint32');
 const requested=parseCompositionRequest(request);
 let selectedBit,selectedMode;
 if(requested==='opaque'){
  if(!(supportedMask&FLAGS.OPAQUE))throw new Error('Opaque Vulkan composition is not supported by this surface');
  selectedBit=FLAGS.OPAQUE;selectedMode='opaque';
 }else if(supportedMask&FLAGS.PRE_MULTIPLIED){
  selectedBit=FLAGS.PRE_MULTIPLIED;selectedMode='pre-multiplied';
 }else if(supportedMask&FLAGS.POST_MULTIPLIED){
  selectedBit=FLAGS.POST_MULTIPLIED;selectedMode='post-multiplied';
 }else throw new Error('Transparent Vulkan composition requested, but the surface supports neither PRE_MULTIPLIED nor POST_MULTIPLIED alpha');
 return {requested,supportedMask,selectedMode,selectedBit,transparent:requested==='transparent'};
}

module.exports={FLAGS,parseCompositionRequest,readSupportedCompositeAlpha,selectCompositeAlpha};
