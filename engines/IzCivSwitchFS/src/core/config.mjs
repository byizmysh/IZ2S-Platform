// Start with detailed wave summaries disabled; controller Y toggles the mode.
export const VERBOSE_LOGGING = false;

export function createFspIdentityConfig() {
  // -- Constants and configuration --------------------------------

  var SLOT_SIZE = 0x800;
  var WAVE_READ_SIZE = 0x10000;    // 64 KiB IStorage::Read per wave
  var DIRTY_FILE_SIZE = 0x800000;  // 8 MiB dirty write
  var MAX_CONTROL_WAVES = 13;      // user-capped per launch
  // Keep runs focused on canary qualification while this flag is enabled.
  var CANARY_BENCH_MODE = false;
  var MAX_COPY_ATTEMPTS = 30;      // per nulWrites step per batch
  var COPY_BATCHES = 3;
  var MAX_ADDRESS_CANDIDATES = 5;
  var MAX_POOL_REFINEMENT_READS = 16;
  var MAX_POOL_ANCHOR_CANDIDATES = 4;
  var MAX_METADATA_ANCHOR_CHECKS = 16;
  var MAX_METADATA_PAGE_PROBES = 16;
  var MAX_ROOT_LAYOUT_SCANS = 4;
  var CANARY_LENGTH = 0x5A0;
  var EXTENT_SIZE = 0x2440;
  var POSITIVE_FILL_SEGMENT = 200;
  var PINNING_ALLOCATION_BYTES = 6960;
  var HEAP_JITTER_BYTES = 192;
  var ALIAS_OFFSET = 0x1400000;    // 20 MiB — alias target
  var MARKER_OFFSET = 0x2340;      // byte offset into cache entry metadata
  var WINDOW_MINIMUM_CACHE_HELD = 0x2600; // reference receive-window floor
  // Extra reference guard applied uniformly to every field copy.
  var FIELD_MINIMUM_CACHE_HELD = 0x2810;
  // Normal alias waves rewrite save:/hole/commit_data (8 MiB) and overlap Commit.
  var MONITORING_ROUNDS = 4;        // extra snapshot re-reads after release
  var CACHE_READ_SIZE = 0x200000;  // 2 MiB widened read
  var CONTROL_SIZE = 0x4000;       // 16 KiB — one cache block
  var ROOT_CAPTURE_SIZE = 0x4D8000;
  var ROOT_CAPTURE_BACK = 0x500000;
  var ROOT_CAPTURE_GAP = 0x28000;
  var ROOT_CAPTURE_CHUNK = 0x28000;
  var ROOT_CAPTURE_CHUNKS = ROOT_CAPTURE_SIZE / ROOT_CAPTURE_CHUNK;
  var CACHE_ALIAS_PREFIX = [
      0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00,
      0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
      0x01, 0x00
  ];

  // Workers are pinned to one core with decreasing priorities so the
  // kernel scheduler fires them in deterministic order after each
  // blocking IPC. The delays create real server-side overlap: BC saves
  // allow concurrent operations (unlike DirectorySaveDataFileSystem's
  // m_accessor_mutex), so the temporal spread is essential for the race.
  var roleDelays = [0, 300000, 350000, 450000, 550000, 650000, 1000000, 1200000, 1300000, 1700000];
  var guardDelays = [150000, 300000];
  // References from Slapah / Kurumuz
  //var roleDelays = [0, 300000, 350000, 450000, 550000, 650000, 1700000, 2000000, 2150000, 2800000];
  //var guardDelays = [0, 150000];
  var roleLabels = ["commit", "bottleneck", "frag0", "frag1", "frag2", "frag3",
      "canary", "filler0", "filler1", "release", "guard0", "guard1", "trigger"];
  var roleShort = ["C", "B", "F0", "F1", "F2", "F3", "Y", "X0", "X1", "R",
      "G0", "G1", "T"];

  // Active worker roles in this wave. Change this single list to add
  // or remove roles without touching the rest of the pipeline.
  // Slot indices are stable (roleLabels[i] is always the same role).
  var workerRoles = [0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12];
  var WORKER_COUNT = workerRoles.length;

  var throughRoles = ["commit", "frag0", "frag1", "frag2", "frag3"];
  var heldRoles = ["canary", "filler0", "filler1", "guard0", "guard1"]
      .filter(function(name) {
          return workerRoles.indexOf(roleLabels.indexOf(name)) >= 0;
      });
  var overlapRoles = throughRoles.concat(heldRoles);
  var OVERLAP_COUNT = overlapRoles.length;

  // Protected roles: all active except bottleneck (1), release (9),
  // trigger (12) — these three have special lifecycle handling.
  var protectedRoles = workerRoles.filter(function(role) {
      return role !== 1 && role !== 9 && role !== 12;
  });
  return {
    SLOT_SIZE,
    WAVE_READ_SIZE,
    DIRTY_FILE_SIZE,
    MAX_CONTROL_WAVES,
    CANARY_BENCH_MODE,
    MAX_COPY_ATTEMPTS,
    COPY_BATCHES,
    MAX_ADDRESS_CANDIDATES,
    MAX_POOL_REFINEMENT_READS,
    MAX_POOL_ANCHOR_CANDIDATES,
    MAX_METADATA_ANCHOR_CHECKS,
    MAX_METADATA_PAGE_PROBES,
    MAX_ROOT_LAYOUT_SCANS,
    CANARY_LENGTH,
    EXTENT_SIZE,
    POSITIVE_FILL_SEGMENT,
    PINNING_ALLOCATION_BYTES,
    HEAP_JITTER_BYTES,
    ALIAS_OFFSET,
    MARKER_OFFSET,
    WINDOW_MINIMUM_CACHE_HELD,
    FIELD_MINIMUM_CACHE_HELD,
    MONITORING_ROUNDS,
    CACHE_READ_SIZE,
    CONTROL_SIZE,
    ROOT_CAPTURE_SIZE,
    ROOT_CAPTURE_BACK,
    ROOT_CAPTURE_GAP,
    ROOT_CAPTURE_CHUNK,
    ROOT_CAPTURE_CHUNKS,
    CACHE_ALIAS_PREFIX,
    roleDelays,
    guardDelays,
    roleLabels,
    roleShort,
    workerRoles,
    WORKER_COUNT,
    throughRoles,
    heldRoles,
    overlapRoles,
    OVERLAP_COUNT,
    protectedRoles
  };
}
