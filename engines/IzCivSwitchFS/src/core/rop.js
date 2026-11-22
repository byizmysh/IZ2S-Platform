// Shared layout and result reader for raw worker frames built in threads.js.

const FRAME_STEP = 0x140;
const FRAME_RESULT = 0x800;
const FRAME_SENTINEL = 0xfedcba9876543210n;

function _frameAlignPage(n) {
    return Math.ceil(n / 0x1000) * 0x1000;
}

function _frameGetArg(call, k) {
    var a = call.args[k];
    return (a === undefined || a === null) ? 0n : BigInt(a);
}

// Read the result of step i from a frame (BigInt, or SENTINEL if not done)
function readFrameResult(frame, i) {
    return read64(frame + BigInt(FRAME_RESULT + i * 8));
}
