let verboseLogging = false;

export function setVerboseLogging(enabled) {
    const previous = verboseLogging;
    verboseLogging = enabled === true;
    return previous;
}

export function isVerboseLogging() {
    return verboseLogging;
}

export function log(message) {
    return globalThis.log(message);
}

export async function verboseLog(message) {
    if (!verboseLogging || !message) return;

    await log(message);
}
