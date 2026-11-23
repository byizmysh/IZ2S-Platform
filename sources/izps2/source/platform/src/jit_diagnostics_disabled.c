/* Release marker only. A zero magic/version explicitly means that publication
 * timing was not measured. Keep the old exported extent for image consumers;
 * never pass valid-looking zero timings off as observations. The release link
 * omits all five diagnostic wrappers and calls the original owners directly.
 * Required arena/domain ownership counters live in their original owners. */
#include "../build/r122-pub-regions-source-v1/ps2_jit_publish_timing.h"
_Static_assert(sizeof(Ps2PubTiming)==512,"Exported diagnostic extent");
__attribute__((visibility("default"),used,aligned(64)))
Ps2PubTiming ps2_hbxl_pub_timing;
