# QLab 5 setup

Use direct Ethernet between controllers and the show-control network. Controllers use DHCP only. For stable QLab patch destinations, give each controller a DHCP reservation on the network; keep QLab's wired interface on the same reachable network.

## QLab → door

In QLab Workspace Settings → Network → Network Outputs, add a patch to each controller's IP and TCP port **53000**. Select **OSC over TCP, SLIP (OSC 1.1)** framing. Do not select UDP, plain text, or OSC 1.0 length-prefix framing.

Create a Network cue containing exactly one of:

```text
/elev-door-front/door/open
/elev-door-rear/door/open
```

Send no arguments. Disable duration/resend behavior; repeated messages intentionally do not extend dwell. Opening from closed and reopening during closing are local behaviors. Commands received before closed-position confirmation or during a settings reservation are ignored. There is no close, hold, stop, or homing command.

## Door → QLab

Configure the controller's QLab host, TCP OSC port (default **53000**), workspace unique ID, and optional OSC passcode. Configure the workspace's OSC Access permissions to allow control via the supplied passcode, or via passcode-less connections if no passcode is set. In particular, cue starts must be permitted. Copy the workspace unique ID from the **Info tab of QLab’s Workspace Status window**. It normally looks like `1B11984A-3EBC-4A9C-A004-B9E3AA32DA6B`; paste only the ID, with no braces or `/workspace/` prefix. `/workspaces` in QLab’s OSC API can also enumerate IDs. This firmware requires the unique ID; leaving it blank cannot enable outgoing cue triggers. QLab itself supports unscoped messages to all listening workspaces, but the controller deliberately targets one workspace.

Check the master **Enable cue triggers** option, enable the **closed** event and enter the exact cue number (for example, `1`), then click **Save configuration**. The host is the QLab Mac’s address, not the controller’s. If saving fails, address the displayed validation error; checkbox changes are not applied until a successful save. **QLab: ready** must appear before testing a complete cycle. Offline with zero reconnect attempts usually means the master enable setting is off.

Each enabled event maps to one cue number. Use a QLab Group cue for multiple actions. The firmware connects to `/workspace/<id>/connect`, enables `/alwaysReply 1`, then queries `/alwaysReply` and waits for confirmation before becoming ready. It sends cue starts as:

```text
/workspace/<id>/cue/<cue-number>/start
```

Default JSON replies are required. Reply-mode confirmation accepts QLab’s JSON boolean `true` as well as the legacy nonzero numeric form. The controller validates the reply envelope, invoked method, optional workspace ID, and successful status. Avoid changing `/replyFormat` for this connection. Authentication accepts legacy `ok` replies and QLab 5 permission-bearing replies such as `ok:view|edit|control`; permission-bearing replies must include `control` to become ready. Invalid passcodes, denied permissions, and QLab errors appear in diagnostics; authentication errors back off for 30 seconds. Connection/reply timeouts reconnect after 2.5 seconds. TCP connect, transmit, and receive are nonblocking; hostname lookup can wait on the separate network core without stopping motor control.

Events offered for mapping: `unknown`, `homing`, `close` (settling before closing), `closed`, `closing`, `open` (settling before opening), `opening`, `reopen`, `reopening`, `waiting`, `fault`, and `forced`. States describe the preserved firmware transitions: **`open` is not an open-limit confirmation**. There is no open limit switch. The `waiting` event can follow successful opening or forced movement; it is not sufficient proof of full travel. Retry pauses remain inside `opening`/`reopening` and do not repeatedly emit those event cues. Use closed-cue eligibility diagnostics to check encoder qualification.

## The closed trigger that advances the sequence

Only a cycle that achieved encoder-confirmed opening and then returned to the closed switch creates a pending closed trigger. The closed switch must have released and the encoder reached travel minus completion tolerance (18,472 microsteps with defaults). Startup, manual initial closure, partial opening, exhausted retries, and fault recovery do not qualify. Faults clear qualification and pending delivery. It occupies one RAM slot. Repeated offers coalesce without extending its original expiry. The default maximum age is **30 seconds**, adjustable in the web page.

If the trigger is pending and QLab is ready, firmware starts its mapped cue. It clears the slot only after QLab's successful reply. If the reply is lost, it reconnects and retries until acknowledged or ineligible. A sent cue may have already run even though no reply was received. **The mapped cue must be designed to tolerate duplicate starts**; this is at-least-once attempted delivery within the expiry window, not exactly-once execution or proof that the whole QLab cue completed.

Do not map this event directly to a repeated relative `GO`/“advance again” action. Use a dedicated cue for the intended sequence step, with a QLab-side guard/latch that ignores subsequent starts for that step; rearm it deliberately at the beginning of the next operating cycle. Simply giving a cue a fixed number does not make it idempotent—QLab can retrigger or restart it. Verify the guard by deliberately dropping replies and observing repeated starts during commissioning.

Faults, reopening, loss of closed confirmation, reboot, expiry, or any accepted settings commit cancels the pending closed trigger. Reconnection alone does not generate a new closed event. A late reply cannot acknowledge a newer pending-slot generation. The controller also suppresses another send while its motor-task snapshot is still clearing an acknowledged slot.

All other events are live-only. Their bounded delivery buffer can absorb brief reply delays while QLab remains connected, but events older than one second or events during a disconnect are discarded. They are never replayed after reconnection and are not retried after uncertain delivery.

References: [QLab OSC dictionary, transport and replies](https://reference.qlab.app/docs/v5/scripting/osc-dictionary-v5/), [QLab Network cues](https://qlab.app/docs/v5/networking/network-cues/).
