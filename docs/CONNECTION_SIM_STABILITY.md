# Dashboard connections and SIM recovery

Wi-Fi association, hostname resolution, HTTP availability, fresh module data and cellular readiness are separate. If `smartvest.local` fails, try `http://192.168.4.1/` while connected to the vest hotspot. If both fail, retain uptime/reset reason, HTTP/radio counters and free/minimum/largest heap before restarting.

## Current HTTP transport

Main uses a project-owned incremental parser and four bounded slots. Each receives at most 512 bytes per UI tick. Header completion expires two seconds after acceptance; the entire request after five seconds. Trickle traffic does not restart these deadlines.

| Input | Limit |
|---|---:|
| Request line plus headers | 2048 bytes |
| Individual line | 1024 bytes |
| Request target including query | 512 bytes |
| Header fields / query arguments | 32 / 16 |
| Declared body | 4096 bytes |

Content-Length is checked before allocation. Ambiguous framing, unsupported transfer encoding/multipart/Expect, malformed CRLF and embedded NUL are rejected. Pipelined bytes cannot become a second request. Signed camera bodies retain exact bytes. Host/origin, session/role and CSRF checks remain authoritative; no SDK patch is required.

Static responses retain flash pointers. Dynamic responses own serialized bytes, capped at 24 KiB per response and 32 KiB total queued response strings, separate from parser/JSON/network memory. Cooperative writes use TCP_NODELAY and at most 1460-byte packets. Idle timeout is two seconds; total dynamic/static deadlines are five/fifteen seconds. Completion means socket submission, not rendering. Completed/failed slots explicitly release String allocations, not just logical text lengths.

Routine administrator/rider/Ride Test requests select scoped views. Full status remains available for diagnostics and can require more memory. Allocation/serialization failures remain visible HTTP 503 responses, not proof of Wi-Fi disconnection. [Allocation metrics/paging](FINALIZATION.md) distinguish failure stages. `bodyTimeouts` can include dispatch waiting after a complete request.

## Browser recovery

### Multi-client Wi-Fi and load recovery (31 October)

Both cameras record video to their own SD cards; they send authenticated control/status traffic to Main. Two cameras plus a phone do not exceed the configured six-client AP limit. Their combined traffic does, however, expose Main/browser memory and retry behavior. Software tests reproduced repeated Ride Test failures continuing at the fastest polling rate and cameras discarding valid sessions on temporary server errors. These are fixed; the reported physical password rejection still needs the matched-build device trial.

Camera timeouts, incomplete replies, HTTP 429 and server errors now retain the established authentication session and the actual failure code for pacing/diagnostics. Untrusted bodies are never accepted as commands. A complete invalid successful response still fails closed, and 401/403 require a new session. This prevents temporary Main overload from producing unnecessary authentication handshakes.

Main also monitors application traffic in addition to AP configuration/IP/DHCP readback. After previously receiving traffic with both cameras present, 45 seconds without any admitted HTTP request can trigger one controlled hotspot restart when a client remains associated. Three authentication/handshake-related disconnect events can also qualify the recovery, including with no currently associated client. A restart waits at least 45 seconds after AP startup and has a 120-second cooldown; another automatic recovery requires actual HTTP traffic to resume. A lone idle phone or empty AP without this prior evidence is not restarted. If both previously seen cameras are deliberately switched off while a phone remains idle, one recovery can occur. Local sensing and camera SD recording are not restarted by this AP action.

Administrator radio diagnostics expose `serviceRecoveries` and `authenticationFailures`; periodic `[RADIO]` lines include `service` and `authFails`. These counters describe observed events, not proof of a wrong password, a power defect or the cause of the original failure. Preserve these lines with heap/HTTP diagnostics if the symptom returns.

Startup has a bounded loading deadline and manual Reload. Failed module loads permit at most one automatic reload per asset version within a minute when storage permits. Reopen old tabs after firmware updates without clearing unexported Ride Test data.

A session alone does not prove data loaded. Initial failures show unavailable updates; successful readings establish current status subject to freshness. Sessions expire 30 minutes from sign-in. [Read scheduling](DASHBOARD_RESPONSIVENESS.md) prioritizes writes/foreground data over pose previews. Writes are never automatically replayed; reload does not restart Main or repeat SMS/calibration commands.

## SIM recovery

UART/flow/awake setup precedes function-mode and SIM queries. SMS setup waits for a readable unlocked card. Confirmed absent/locked/read-failed states cause periodic checks rather than repeated UART resynchronization. Function mode is rechecked at most once per minute while waiting; an enabled/searching radio is not needlessly restarted. Genuine communication timeouts retain recovery.

All `AT+CPIN?` paths use a 5.5-second cooperative deadline: the documented five-second SIM query limit plus 0.5 seconds for UART/service scheduling. Startup, waiting-card checks, background polling, network recovery and SMS preflight use the same limit. Shorter 1.8/2.2-second waits previously caused recovery to overtake a delayed SIM reply. Explicit errors are processed immediately. A timeout without a terminal OK/error, including a partial CPIN status, requires command-mode synchronization before another query; silent queries retain recovery.

A fresh `+CME ERROR: 10` to `AT+CPIN?` is the modem reporting an inaccessible/undetected card; it is not a signal-strength or network-registration result. An explicit response must not be replaced by guessed readiness. See the [SIMCom AT command manual](https://simcom.ee/documents/SIM800C/SIM800%20Series_AT%20Command%20Manual_V1.10.pdf), CPIN section and CME error definitions.

Holder notifications invalidate old identity and request verification; they do not independently establish physical absence. Hot insertion is not guaranteed without suitable holder/detection wiring and configuration. An unreadable SIM keeps SMS unavailable. Check seating, holder/adapter contacts, card interface and suitable supply with a known-working card if fresh queries still report absence. UART activity does not establish SIM access or registration.

Submission requires both `+CMGS` and final `OK`. Carrier reports and handset receipt remain separate. Unknown submitted outcomes are not blindly resent after reboot. Cameras record locally; Main carries authenticated control/status, not video streams.

## Device verification

Repeated six-white-flash startup sequences mean Main re-entered startup; an AP recovery alone does not run that LED sequence. After reconnecting, open `http://192.168.4.1/admin/system` with the developer account and record **Main ESP32 → Reset reason** before cycling power. `Panic` identifies a software exception/abort category, not the exact failing function; `Brownout` is a different reset reason. The serial startup report also records the numeric reason.

Main now owns a bounded, nonblocking UDP DNS responder on its dashboard task. It handles one datagram per tick, bounds every label by the received length, drops malformed/compressed-question/oversized requests, returns the vest IPv4 address for A/ANY, and returns no address for other query types. It does not use the core 3.3.12 DNSServer/AsyncUDP callback. Inspection found an unbounded `strchr` on incoming DNS packet memory in that core; an unterminated packet reproduced an out-of-bounds fault in a protected-memory host test. This is a verified fault path, not proof of the user's exact physical panic without its backtrace. Existing HTTP-ready-before-AP ordering and bounded AP recovery remain in place.

[Retained records](VERIFICATION.md) document software outcomes, not an exclusive physical cause of a stall. Follow the [65-minute acceptance workload](HARDWARE_ACCEPTANCE.md), capturing 9600-baud `[MEM]`/`[HTTP MEMORY]` lines, endpoint failure stages, heap, camera freshness, modem service gaps and reset reason.
