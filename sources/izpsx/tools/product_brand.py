"""Apply IZPSX emulator presentation to generated copies of the core."""
import re


def apply(relative, source):
    if relative == 'frontend/libretro.c':
        replacements = {
            'info->library_name     = "PCSX-ReARMed";':
                'info->library_name     = IZPSX_PRODUCT_NAME;',
            'info->library_version  = "r26" GIT_VERSION;':
                'info->library_version  = IZPSX_VERSION;',
            'LogErr("PCSX init failed.\\n");':
                'LogErr("IZPSX emulator initialization failed.\\n");',
        }
        for old, new in replacements.items():
            assert source.count(old) == 1, (relative, old)
            source = source.replace(old, new)
    elif relative == 'frontend/main.c':
        old = '"Starting PCSX-ReARMed " REV " (%s)\\n"'
        assert source.count(old) == 1, (relative, old)
        source = source.replace(old, '"Starting " IZPSX_PRODUCT_NAME " " IZPSX_VERSION " (%s)\\n"')
        source = source.replace('"PCSX-ReARMed " REV "\\n"',
                                'IZPSX_PRODUCT_NAME " " IZPSX_VERSION "\\n"')
    elif relative in ('frontend/libretro_core_options.h',
                       'frontend/libretro_core_options_intl.h'):
        # Only displayed descriptions change. Option keys and enum values are
        # the core's compatibility API, and copyright notices stay intact.
        def displayed(match):
            return re.sub(r'PCSX[- ]ReARMed', 'IZPSX emulator', match[0], flags=re.I)
        return re.sub(r'"(?:\\.|[^"\\])*"', displayed, source)
    else:
        return None
    source = source.replace('"PCSX"', 'IZPSX_PRODUCT_NAME')
    return '#include "brand.h"\n' + source
