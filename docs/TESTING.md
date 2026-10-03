# Bench and fault test plan

Use all eight configured boards, seven joysticks, eight RGB LEDs, and the Master USB dashboard. Start with all LEDs off, all joysticks centered, 8/8 online, and reset complete. Between cases that specify a new start, reset and wait for the barrier before selecting that player.

## Required acceptance cases

| Test | Procedure | Expected |
|---|---|---|
| 1 | Select Jyothi; Start | Jyothi alone green; holder ID 2 |
| 2 | Reset/start each of IDs 2–8 in turn | Only the selected player green; initial source Master |
| 3 | Start Jyothi; move right | Abhishek green; Jyothi off |
| 4 | Start Abhishek; move left | Jyothi green |
| 5 | Start Abhishek; move right | Durgamani green |
| 6 | Start Durgamani; move right | Shreesathya green |
| 7 | Start Shreesathya; move left | Durgamani green |
| 8 | Start Sanjeevini; move left | Rishab green |
| 9 | Start Jyothi; move left | No transfer; Jyothi retains ball; error says no neighbor |
| 10 | Start Sanjeevini; move right | No transfer; Sanjeevini retains ball; same error |
| 11 | Move an inactive player's stick | No transfer, no activation; recenter after gaining the ball |
| 12 | Unplug a slave | Offline within about 7–8 seconds; no invented replacement owner |
| 13 | Drop transfer/ACK packets using the fault settings below | Same transaction retried; timeout reported; sender never reactivates; Retry recovers after loss ends |
| 14 | Duplicate BALL_PASS using fault setting below | One receiver activation, at most one green slave; subsequent return passes still work |
| 15 | Reset during play and during a pending transfer | Reachable boards clear immediately; all return to idle/waiting after ACKs; no new start before all seven clear |

## Full example

Start Durgamani → left to Abhishek → right to Durgamani → left to Abhishek → left to Jyothi → attempt left (reject) → right to Abhishek. Center the newly active joystick before every gesture. Check the physical LEDs, dashboard holder, sequence progression, and event routes after each step.

## Fault injection

`firmware/VirtualBall/config.h` has disabled-by-default bench options:

- `DROP_TX_TYPE = 6`: suppress all `BALL_RECEIVED` application ACKs on a receiver. Sender offers retry, receiver remains staged, and no slave is activated. Set back to zero after the test. Reflashing/rebooting starts a reset barrier; use native tests to check same-session recovery without reboot.
- `DROP_TX_TYPE = 9`: suppress Master `GRANT` delivery. A transfer has a designated target but no active owner. Timeout is explicit; do not restore the sender.
- `DROP_TX_TYPE = 10`: suppress `GRANT_ACK`. Receiver may be physically green while Master reports confirmation pending. The next valid pass can still establish the handoff safely.
- `DROP_EVERY_N = 4`: drop every fourth outgoing packet on the boards built with it, for repeatable lossy traffic. This is simulated loss before radio submission and will not increment low-level MAC send failures.
- `DUPLICATE_TX_TYPE = 5`: enqueue every `BALL_PASS` twice on a sender. Ball count remains one.

Type numbers are in `protocol.h`/README. Apply changed firmware consistently when changing common settings, restore all three options to zero after fault testing, and wait for the all-board reset. The final defaults contain real ESP-NOW transport with fault injection disabled.

`tests/game_test.cpp` drops each handoff type independently without rebooting devices, then restores delivery and retries. It also replays old start/grant/reset packets and verifies no duplicate ball. Its simulator is test-only and drives the actual `game.cpp` engine; it is not used in the ESP32 build.

## Additional reliability tests

1. **Hold gesture:** hold a joystick fully right for five seconds, including while the ball returns. Exactly one pass per neutral-to-direction gesture; it cannot pass again until recentered.
2. **Vertical/deadzone:** small jitter and up/down movement never pass. A short direction spike under 65 ms is ignored.
3. **Reboot owner:** restart the green board. It boots inactive; Master detects its boot counter and resets everyone before another game.
4. **Reboot Master with unreachable owner:** keep a slave powered with its old ball but out of Master range, then reboot Master. A new game remains blocked until the old owner reconnects and clears.
5. **Reset while target powered off:** reset stays pending and the missing player's name is displayed. Reconnect it; the barrier completes. A finite retry error may be logged before it rejoins.
6. **Late ACK and duplicate grant:** use the native replay test; old grants cannot revive a surrendered ball, and duplicate current grants cannot rearm a held joystick.
7. **Wrong route/source/epoch/boot:** native tests reject non-neighbor handoffs, sender MAC mismatch, wrong destination, invalid CRC/length, old epoch, and prior-boot activation.
8. **Serial splits:** native JavaScript parser tests split a JSON line across chunks, combine multiple lines, insert boot chatter, and discard an oversized line before recovering.
9. **Wrong USB board:** select a slave in the browser; connection closes and controls remain disabled with a clear message.
10. **Disconnect/reconnect:** unplug Master USB and verify stale/closed UI, then reconnect to rebuild configuration and current status. Ensure serial locks are released so reconnect works.
11. **Responsive UI:** inspect desktop and phone-width layouts; all eight cards remain in physical order and controls remain readable. Web Serial itself is intended for desktop browsers.
12. **Persistent storage failure:** native test makes an epoch write fail; the Master halts and cannot start a new game.

## Automated commands

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
python -m platformio run
```

Optional browser integration check (Python Playwright and installed Chrome):

```powershell
python -m pip install playwright
python tests/browser_test.py
```

Browser integration uses an explicitly mocked Web Serial device to exercise the real dashboard. It is not proof of USB hardware or ESP-NOW radio operation. Physical tests must still be performed on the eight boards.
