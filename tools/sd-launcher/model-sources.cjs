'use strict';
const imageSources=['core/console/sd-launcher/relocatable/loader.c','core/console/sd-launcher/relocatable/rebase.c','core/console/sd-launcher/relocatable/address.c'];
module.exports={imageSources,legacySources:[...imageSources,'tests/sd-launcher/legacy-self.c']};
