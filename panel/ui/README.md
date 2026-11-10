The 0.22 HTTP entry point is `panel.html`. Keep it in sync with `app.js`,
`civswitch.js` and `cardputer-setup.js`. `startup.js` reports errors that happen
before application polling starts. An old `index.html` is not served.

The UI contract tests check the actual HTTP entry point and initialize the
CivSwitch controller against its controls.

Run `node sdk/build-panel-ui.cjs` after editing `storage.js`, `startup-choices.js`, `presentation.js`, error text, or either ARM installer template in `sdk/`. It embeds those
controls into the served `app.js`, so updating the UI does not require restarting
an existing server just to register another asset route.

The first UI read uses `/api/startup`, with a two-minute client deadline and
one shared inventory read across windows and retries. Cold archive verification
can exceed the ordinary ten-second status deadline. The loading screen names
this preparation and opens the panel automatically when it completes. Subsequent
polls use `/api/state` and retain their short deadline. Startup only reads state;
it does not start console input or resume a console session.

Desktop release switches share a startup lock. The previous verified IZ2S
server receives SIGTERM, followed by SIGKILL after eight seconds if it remains
alive. Only workers captured under that owner are eligible; PID start times
are rechecked, and desktop browsers are excluded. A forced stop records that
Switch still needs a full restart; it does not prove native console cleanup.

The latest session journal is restored as history after a panel restart.
It never resumes console input or adopts an old native session. Late FS
diagnostics are merged into the panel log and the full log download, with a
separate note when they were recorded after an explicit PC-side stop.

Civ7 console standby uses `awaiting-loader`, independently of a verified link.
Returning to First Setup begins at step one; only a changing current operation
advances the displayed step. Historical progress stays available for recovery.

RAM startup loads only explicit module IDs. Historical installed-session records
never populate a new launcher. The panel keeps suggestions separately for each
game and shell; loading is disabled on page load and when that context changes.
Legacy global startup preferences are not imported.

## Release 0.22 connection presentation

Engine/mode labels share a heading row with their launcher/First Setup/Launch choices. SB4 Launch names Make a Program → Direct. Civ7 Launch exposes synchronized LAN/Wi-Fi icon selectors on PC and Console; they select the intended console transport rather than changing OS networking. Initial LDN prerequisites affect First Setup, not the running PC service's Launch status. Finished historical errors are not recreated as new notifications.
