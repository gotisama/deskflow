# Asymmetric Screen Switch Rules — Design

Date: 2026-05-14
Status: Approved (design phase)
Scope: Server Configuration → Advanced tab

## Problem

Deskflow's edge-based screen switching applies the same rules in both directions
(server→client and client→server). A user wants asymmetric behavior:

- Leaving the server (server → client) must require holding a modifier key (Ctrl by default)
  so the cursor doesn't slip into the client by accident.
- Returning to the server (client → server) must be instantaneous — no wait delay,
  no double-tap, no modifier requirement.

The existing `switchNeedsShift/Control/Alt` options exist but apply to both directions
uniformly. There is no per-direction control today.

## Goals

- Let the user configure asymmetric switch rules via two new checkboxes in the
  Server Configuration → Advanced → Switching group.
- Preserve full backward compatibility: when neither checkbox is enabled, behavior
  is identical to today.
- Coexist with existing wait-delay, double-tap, and global modifier-needed options.

## Non-Goals

- Generalizing to N-direction asymmetric rules (e.g., per-edge or per-client).
- Replacing or migrating away from existing switch options.
- Changing the `[options]` config file grammar in a backward-incompatible way.

## User-Visible Behavior

Two new controls appear in the Switching group of the Advanced tab, after the
existing "Switch on double tap" row:

```
☐ Hold [Ctrl ▼] to leave server
☐ Return to server without waiting
```

- The modifier ComboBox offers Ctrl (default selection), Shift, Alt.
- The ComboBox is disabled when its checkbox is unchecked.
- The two checkboxes are independent of each other (no enable/disable coupling).

Behavior matrix:

| Hold modifier | Instant return | Server → Client                    | Client → Server                       |
|---------------|----------------|------------------------------------|---------------------------------------|
| ☐             | ☐              | Current behavior                   | Current behavior                      |
| ☑ (Ctrl)      | ☐              | Edge hit AND Ctrl held → switch    | Current behavior (wait/double-tap)    |
| ☐             | ☑              | Current behavior                   | Edge hit → switch immediately         |
| ☑ (Ctrl)      | ☑              | **Target use case:** Ctrl + edge   | Instant on edge hit, no modifier      |

"Current behavior" = existing `switchDelay`, `switchDoubleTap`,
`switchNeedsShift/Control/Alt` apply as configured.

When "Return to server without waiting" is enabled, the client→server direction
bypasses:

- `switchDelay` (wait delay)
- `switchDoubleTap` (double-tap requirement)
- `switchNeedsShift/Control/Alt` (global modifier requirement)
- The new `leaveServerNeedsModifier` (which logically wouldn't apply going *into*
  the server anyway, but is excluded explicitly for clarity)

It does NOT bypass:

- "No neighbor exists" (structural — no destination)
- "Locked to screen" (user explicitly locked)
- "Locked in dead corner" (safety configuration)

These are correctness/safety constraints, not UX gates.

## Architecture

The change is local. No new modules, no new abstractions.

```
┌─────────────────────────────────────┐
│ ServerConfigDialog (Qt UI)          │
│  - 2 new widgets bound to fields    │
└──────────────┬──────────────────────┘
               │ (Qt data binding)
┌──────────────▼──────────────────────┐
│ ServerConfig (GUI model, QSettings) │
│  - 3 new fields + getters/setters   │
│  - Serializes to .conf text         │
└──────────────┬──────────────────────┘
               │ (config file text)
┌──────────────▼──────────────────────┐
│ Config (server, parses .conf)       │
│  - 2 new keywords in readOption()   │
│  - Writes 2 new fields onto Server  │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ Server (core, runtime logic)        │
│  - 3 new fields                     │
│  - isSwitchOkay() direction-aware   │
└─────────────────────────────────────┘
```

## Detailed Design

### UI — `src/lib/gui/dialogs/ServerConfigDialog.ui` + `.cpp/.h`

In the Switching group (currently lines 673-816 in the .ui file), append two
rows after the existing "Switch on double tap" row:

- Row 1: `QCheckBox cbLeaveServerNeedsModifier` with text "Hold modifier to leave
  server", followed by `QComboBox cmbLeaveServerModifier` with items
  `["Ctrl", "Shift", "Alt"]` (index 0 = Ctrl default).
  - The ComboBox's `enabled` property is bound to the checkbox's `checked` state.
- Row 2: `QCheckBox cbReturnToServerInstant` with text "Return to server without
  waiting".

In `ServerConfigDialog.cpp`:
- In the constructor (or wherever existing widgets bind), wire the 3 widgets'
  values to and from `m_serverConfig` (the `ServerConfig` instance).
- Hook the leave-server checkbox's `toggled(bool)` signal to set the ComboBox's
  `enabled` property.

### Data model — `src/lib/gui/config/ServerConfig.h/cpp`

Add private fields next to `m_SwitchDelay` etc. (around line 243-246):

```cpp
bool m_LeaveServerNeedsModifier = false;
int  m_LeaveServerModifier      = 0;   // 0=Ctrl, 1=Shift, 2=Alt
bool m_ReturnToServerInstant    = false;
```

Add 3 getter/setter pairs following the existing naming pattern (e.g.,
`hasLeaveServerNeedsModifier()`, `setLeaveServerNeedsModifier(bool)`,
`leaveServerModifier()`, `setLeaveServerModifier(int)`, etc.).

Persistence:
- `ServerConfig::save()` / `loadSettings()` (QSettings): add 3 read/write lines
  matching existing patterns for `m_SwitchDelay`.
- Serialization to `.conf` text: in the function that emits `switchDelay` and
  similar keywords, append:
  - If `m_LeaveServerNeedsModifier`: emit `leaveServerNeedsModifier = <ctrl|shift|alt>`
  - If `m_ReturnToServerInstant`: emit `returnToServerInstant = true`

### Config parsing — `src/lib/server/Config.cpp`

In `Config::readOption()` (around line 657, where `switchDelay` etc. are parsed),
add two new branches:

```cpp
else if (CaselessCmp::equal(name, "leaveServerNeedsModifier")) {
    // value: "none" | "shift" | "control" | "alt" (caseless)
    // Maps to m_server->setLeaveServerNeedsModifier(...) and modifier mask
}
else if (CaselessCmp::equal(name, "returnToServerInstant")) {
    // value: "true" | "false"
    // Maps to m_server->setReturnToServerInstant(...)
}
```

Output side: in the function that writes `switchDelay` and friends back to text,
emit the two new keywords iff the corresponding flag is set.

### Runtime logic — `src/lib/server/Server.h/cpp`

Add to `Server.h` near `m_switchNeedsControl` (around line 384-406):

```cpp
bool m_leaveServerNeedsModifier = false;
KeyModifierMask m_leaveServerModifierMask = KeyModifierControl;
bool m_returnToServerInstant = false;
```

Plus public setters `setLeaveServerNeedsModifier(KeyModifierMask)` (or two
arg variant), `setReturnToServerInstant(bool)` callable from `Config`.

Modify `Server::isSwitchOkay()` (Server.cpp:765). At the top, derive direction
flags:

```cpp
const bool leavingServer  = (m_active   == m_primaryClient);
const bool enteringServer = (newScreen  == m_primaryClient);
```

Then update three gates:

1. Double-tap gate (currently lines 793-803): skip the gate entirely when
   `enteringServer && m_returnToServerInstant`.
2. Wait-delay gate (currently lines 806-811): skip when
   `enteringServer && m_returnToServerInstant`.
3. Modifier gate (currently lines 846-853): rewrite to OR in the new
   leave-server requirement when `leavingServer`, and zero out all modifier
   requirements when `enteringServer && m_returnToServerInstant`.

The three structural gates — no-neighbor (lines 772-778), corner-lock
(lines 819-836), screen-lock (lines 839-843) — are unchanged.

## Testing

Add unit tests in `src/unittests/server/` covering `isSwitchOkay()` decisions.
Use mocks/fakes of `BaseClientProxy` already established by existing tests.

Four matrix scenarios as listed in the behavior table above:

1. Both off → regression test that current behavior is preserved.
2. Only `leaveServerNeedsModifier=Ctrl`:
   - Leaving server without Ctrl pressed → returns false.
   - Leaving server with Ctrl pressed → returns true.
   - Entering server still walks the wait-delay path.
3. Only `returnToServerInstant=true`:
   - Leaving server still goes through wait-delay.
   - Entering server bypasses wait-delay, double-tap, global-modifier checks.
4. Both on → target scenario, both rules apply.

Existing wait/double-tap unit tests remain untouched (regression safety net).

Manual verification:
- Windows server + Linux/Mac client.
- All 4 matrix rows.
- Multi-client layout (one client left, one right): Ctrl-gate applies to both
  outgoing edges; both return paths are instant.

## Backward Compatibility

- Both new fields default to `false`. A user who upgrades and changes nothing
  sees identical behavior.
- New `.conf` keywords are additive. Old .conf files parse unchanged.
- New GUI fields don't conflict with the existing (internal-only)
  `switchNeedsShift/Control/Alt`; users who somehow set both get the union of
  requirements when leaving the server.

## Open Questions

None at design time. Implementation may surface naming or signal-wiring details
to confirm during code review.
