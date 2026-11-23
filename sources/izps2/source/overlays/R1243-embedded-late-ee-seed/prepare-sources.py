"""Keep the R1240 boot path and add an opt-in, checkpoint-bound EE warm suffix."""

from pathlib import Path

out = Path(__file__).resolve().parent
base = out.parent / 'R1240-ee-vu1-core-separation'


def replace_once(source, before, after):
    assert source.count(before) == 1, before
    return source.replace(before, after)


inc = (base / 'ps2_sb4_ee_warm.inc').read_text()
inc = replace_once(inc, '#include "checkpoint-burst-seed.h"',
                   '#include "checkpoint-burst-seed.h"\n#include "checkpoint-late-suffix.h"')
inc = replace_once(inc, 'bool warm_compile_burst, bool read_burst_extension)',
                   'bool warm_compile_burst, bool read_burst_extension, bool embedded_late_requested)')
inc = replace_once(inc, '''                        accepted ? embedded_seed.size() : 0);
                }
            }''', '''                        accepted ? embedded_seed.size() : 0);
                }
                else if (embedded_late_requested)
                {
                    constexpr size_t late_bytes = sizeof(izps2_checkpoint_late_suffix);
                    constexpr size_t total_bytes = sizeof(izps2_checkpoint_burst_seed) + late_bytes;
                    constexpr u32 entries = (total_bytes - ps2::ee_warm::HeaderBytes) / ps2::ee_warm::RecordBytes;
                    static_assert(late_bytes % ps2::ee_warm::RecordBytes == 0);
                    static_assert(entries <= ps2::ee_warm::CheckpointMaxEntries);
                    late_extension.reserve(total_bytes);
                    late_extension.assign(embedded_seed.begin(), embedded_seed.end());
                    late_extension.insert(late_extension.end(), std::begin(izps2_checkpoint_late_suffix),
                                          std::end(izps2_checkpoint_late_suffix));
                    for (unsigned byte = 0; byte < sizeof(entries); ++byte)
                        late_extension[16 + byte] = static_cast<u8>(entries >> (8 * byte));
                    const bool accepted = checkpoint_burst_extension_matches(late_extension);
                    if (accepted)
                        embedded_seed = late_extension;
                    Console.WriteLn("IZPS2_EE_EMBEDDED_LATE accepted=%u entries=%u suffix_bytes=%zu",
                        unsigned(accepted), accepted ? entries : 0, late_bytes);
                }
            }''')
(out / 'ps2_sb4_ee_warm.inc').write_text(inc)

runner = (base / 'ps2_vm_runner.cpp').read_text()
runner = replace_once(runner, '    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {', '''    const bool embedded_late_requested =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "ee-checkpoint-embedded-late.flag");
#else
        false;
#endif
    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {''')
runner = replace_once(runner, 'extend_embedded_seed, warm_compile_burst, read_burst_extension);',
                      'extend_embedded_seed, warm_compile_burst, read_burst_extension, embedded_late_requested);')
runner = replace_once(runner, 'neutral_requested && ee_warm.read_only_seed && !read_burst_extension &&',
                      'neutral_requested && ee_warm.read_only_seed && !read_burst_extension && !embedded_late_requested &&')
(out / 'ps2_vm_runner.cpp').write_text(runner)
