// Render logs in one styled pre and forward them to persistence when available.
const MAX_VISIBLE_LOG_LINES = 100;
let clearCurrentVisualLog = function () {};

export function clearVisualLog() {
    const hostWindow = globalThis.window || globalThis;
    let logger = hostWindow.log;
    if (typeof logger?.clear !== "function") {
        logger = globalThis.log;
    }
    if (typeof logger?.clear === "function")
        return Reflect.apply(logger.clear, logger, []);

    clearCurrentVisualLog();
}

export function installLoaderLogPatch() {
    const hostWindow = globalThis.window || globalThis;
    if (typeof hostWindow.log?.clear === "function") return;

    const document = globalThis.document;
    let output = null;
    let compact = false;
    const visibleLines = [];

    clearCurrentVisualLog = function () {
        visibleLines.length = 0;
        if (output?.isConnected) output.textContent = "";
    };

    // The applet presents frames between macrotasks: raw log() bursts
    // (no per-line pause like emitStage's 30 ms) append DOM rows that
    // never paint — a whole payload stage can run invisible (vsdump
    // 2026-11-16). waitForDisplayFrame below paces every patched log
    // call so unpaced callers stay visible too.

    function ensureOutput() {
        if (!document?.body) return null;

        if (!output?.isConnected) {
            visibleLines.length = 0;
            output = document.createElement("pre");
            document.body.appendChild(output);
        }

        // This patch owns the only visible log buffer.
        for (const nativeOutput of document.querySelectorAll("pre")) {
            if (nativeOutput !== output && nativeOutput.parentNode)
                nativeOutput.parentNode.removeChild(nativeOutput);
        }

        compact = applyConsoleStyle(output, document);
        return output;
    }

    const patchedLog = function (...args) {
        const timestamp = formatTimestamp(new Date());
        const message = String(args[0]);
        const messageLines = message.replace(/\r\n?/g, "\n")
            .split("\n");

        const logOutput = ensureOutput();
        if (logOutput) {
            appendLogRows(logOutput, document, messageLines, timestamp,
                compact, visibleLines);
            logOutput.scrollTop = logOutput.scrollHeight;
        }

        forwardPersistedLog(message);
        return waitForDisplayFrame(hostWindow);
    };

    patchedLog.clear = clearCurrentVisualLog;
    hostWindow.log = patchedLog;
    globalThis.log = patchedLog;
    ensureOutput();
}

function forwardPersistedLog(message) {
    const forwarder = globalThis.__CIVSWITCHFS_PERSIST_LOG_FORWARDER__;
    if (typeof forwarder !== "function") return;

    try {
        Reflect.apply(forwarder, globalThis, [message]);
    } catch (error) {}
}

function waitForDisplayFrame(hostWindow) {
    let requestFrame = hostWindow.requestAnimationFrame;
    let frameWindow = hostWindow;
    if (typeof requestFrame !== "function") {
        requestFrame = globalThis.requestAnimationFrame;
        frameWindow = globalThis;
    }
    if (typeof requestFrame !== "function") return Promise.resolve();

    return new Promise(resolve => requestFrame.call(frameWindow, resolve));
}

function formatTimestamp(date) {
    const twoDigits = value => String(value).padStart(2, "0");
    const threeDigits = value => String(value).padStart(3, "0");
    return twoDigits(date.getHours()) + ":" +
        twoDigits(date.getMinutes()) + ":" +
        twoDigits(date.getSeconds()) + "." +
        threeDigits(date.getMilliseconds());
}

function applyConsoleStyle(output, document) {
    const viewportWidth = Number(document?.documentElement?.clientWidth) ||
        Number(globalThis.innerWidth) || 0;
    const compact = viewportWidth > 0 && viewportWidth <= 700;
    let fontSize;

    if (compact) {
        fontSize = "15px";
    } else {
        fontSize = "18px";
    }

    output.setAttribute("aria-label", "CivSwitchFS logs");
    output.setAttribute("role", "log");
    Object.assign(output.style, {
        position: "fixed",
        top: "0",
        left: "0",
        right: "0",
        bottom: "0",
        zIndex: "2147483647",
        boxSizing: "border-box",
        margin: "0",
        padding: "10px 0 22px",
        overflowY: "auto",
        overflowX: "hidden",
        fontFamily: "Consolas, 'Liberation Mono', monospace",
        fontSize,
        lineHeight: "1.5",
        background: "#282c34",
        color: "#f2f3f5",
        whiteSpace: "normal",
        overscrollBehavior: "contain"
    });
    return compact;
}

function appendLogRows(output, document, lines, timestamp, compact,
    visibleLines) {
    for (const line of lines.slice(-MAX_VISIBLE_LOG_LINES)) {
        const row = document.createElement("div");
        Object.assign(row.style, {
            display: "flex",
            alignItems: "center",
            boxSizing: "border-box",
            padding: compact ? "3px 10px" : "3px 14px",
            fontSize: compact ? "15px" : "18px",
            lineHeight: "1.5"
        });

        const stamp = document.createElement("span");
        stamp.textContent = timestamp;
        Object.assign(stamp.style, {
            color: "#9bcaff",
            flex: "0 0 13ch",
            whiteSpace: "nowrap",
            fontSize: compact ? "12px" : "14px",
            lineHeight: "1.5",
            userSelect: "none",
            marginRight: compact ? "12px" : "18px"
        });

        const content = document.createElement("span");
        content.textContent = line || "\u00a0";
        Object.assign(content.style, {
            minWidth: "0",
            color: "#e8ece8",
            whiteSpace: "pre-wrap",
            overflowWrap: "break-word",
            wordBreak: "break-word"
        });

        row.appendChild(stamp);
        row.appendChild(content);
        output.appendChild(row);
        visibleLines.push(row);

        while (visibleLines.length > MAX_VISIBLE_LOG_LINES) {
            const oldestLine = visibleLines.shift();
            if (oldestLine?.parentNode === output)
                output.removeChild(oldestLine);
        }
    }
}

export function installRunInputControls({
    onResetTimings, onStop, onToggleVerbose
} = {}) {
    const window = globalThis.window;
    if (!window?.addEventListener) return {dispose() {}};

    const statuses = window.InputActionStatuses || {
        START: 0,
        FINISH: 1,
        UPDATE: 2
    };
    let disposed = false;
    let inputTimer = null;
    let lastResetRequest = 0;
    let runCallbacks = {onResetTimings, onStop, onToggleVerbose};
    let controllerPrevious = Object.create(null);
    let lastVerboseToggleAt = 0;
    const engineActionActive = {reset: false, stop: false};

    function consume(event) {
        event?.preventDefault?.();
        event?.stopImmediatePropagation?.();
    }

    function requestReset(event) {
        if (typeof runCallbacks.onResetTimings !== "function") return false;
        consume(event);
        const now = Date.now();
        if (now - lastResetRequest < 100) return true;
        lastResetRequest = now;
        runCallbacks.onResetTimings();
        return true;
    }

    function requestStop(event) {
        if (typeof runCallbacks.onStop !== "function") return false;
        consume(event);
        runCallbacks.onStop();
        return true;
    }

    function requestToggleVerbose() {
        if (typeof runCallbacks.onToggleVerbose !== "function") return false;
        const now = Date.now();
        if (now - lastVerboseToggleAt < 180) return true;
        lastVerboseToggleAt = now;
        runCallbacks.onToggleVerbose();
        return true;
    }

    function handleEngineAction(event, action) {
        let callback;
        if (action === "reset") {
            callback = runCallbacks.onResetTimings;
        } else {
            callback = runCallbacks.onStop;
        }
        if (typeof callback !== "function") return;

        const isFinished = event.detail?.status === statuses.FINISH;
        const isActive = engineActionActive[action];
        consume(event);
        if (!isActive) {
            if (action === "reset") requestReset(event);
            else requestStop(event);
        }
        engineActionActive[action] = !isFinished;
    }

    function handleEngineToggle(event) {
        if (typeof runCallbacks.onToggleVerbose !== "function") return;
        const status = event.detail?.status;
        const isStart = status === statuses.START ||
            String(status).toLowerCase() === "start";
        const isFinished = status === statuses.FINISH ||
            String(status).toLowerCase() === "finish";
        if (!isStart && !isFinished && status != null) return;
        consume(event);
        if (!isFinished) requestToggleVerbose();
    }

    function onEngineInput(event) {
        if (disposed) return;
        const detail = event.detail || {};
        let name = "";
        if (typeof detail.name === "string") {
            name = detail.name.toLowerCase();
        }
        let isCancel = false;
        // Use the game's cancel helper when available; raw action names vary.
        try {
            isCancel = typeof event.isCancelInput === "function" &&
                event.isCancelInput() === true;
        } catch (ignored) {}

        if (isCancel || name === "back" ||
            name === "cancel" || name === "ui-back") {
            handleEngineAction(event, "stop");
            return;
        }

        if (name === "shell-action-1") {
            handleEngineAction(event, "reset");
            return;
        }

        if (name === "shell-action-2" || name === "shell-action-y" ||
            name === "button-y" || name === "gamepad-y" || name === "y") {
            handleEngineToggle(event);
            return;
        }
    }

    function pollGamepad() {
        let navigator = globalThis.navigator;
        if (typeof window.navigator?.getGamepads === "function") {
            navigator = window.navigator;
        }
        if (!navigator || typeof navigator.getGamepads !== "function") return;

        let pads;
        try {
            pads = navigator.getGamepads();
        } catch (ignored) {
            return;
        }

        let pad = null;
        let fallbackPad = null;
        for (let index = 0; index < pads.length; index++) {
            const candidate = pads[index];
            if (!candidate || candidate.connected === false) continue;
            if (!fallbackPad && candidate.buttons?.length >= 4)
                fallbackPad = candidate;
            if (candidate.mapping === "standard") {
                pad = candidate;
                break;
            }
        }
        if (!pad) pad = fallbackPad;
        if (!pad) {
            controllerPrevious = Object.create(null);
            return;
        }

        function pressed(index) {
            return Boolean(pad.buttons[index] && pad.buttons[index].pressed);
        }

        // Standard Gamepad layout: Nintendo B is south (0), Y is west (2), X is north (3).
        for (const button of [0, 2, 3]) {
            const current = pressed(button);
            if (current && controllerPrevious[button] !== true) {
                if (button === 3) requestReset();
                else if (button === 2) requestToggleVerbose();
                else requestStop();
            }
            controllerPrevious[button] = current;
        }
    }

    window.addEventListener("engine-input", onEngineInput, true);

    inputTimer = setInterval(pollGamepad, 40);

    const controls = {
        clearRunActions() {
            runCallbacks = {onResetTimings: null, onStop: null,
                onToggleVerbose: null};
        },
        dispose() {
            if (disposed) return;
            disposed = true;
            if (inputTimer !== null) clearInterval(inputTimer);
            window.removeEventListener("engine-input", onEngineInput, true);
        }
    };
    return controls;
}
