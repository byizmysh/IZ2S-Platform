const coreApi = globalThis.__CIVSWITCH_CORE_API__;
if (!coreApi) throw new Error("tools/bundle.mjs did not initialize the core runtime API");

export default coreApi;
export const waitMs = coreApi.waitMs;
