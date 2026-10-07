# ESP32 Wi-Fi source and virtual checks

Checked on 2026-10-08 for NES-SDR issue 1.

## Reproduced error and correction

The RF startup service shuts down setup HTTP and the AP before allocating the
RF task. In the previous code, errors inside that task at Wi-Fi start,
power-save setup, promiscuous enable or channel setup deleted the task and
left setup offline. The error state also allowed a new RF start immediately.

Native fault injection reproduced all four cases against the production
`nes_sdr_platform.c`. The correction queues recovery through the resident
service after the failed RF task exits. Recovery disables promiscuous mode,
restarts the setup AP and HTTP, and rejects another start while pending.
The resident task yields 500 ms to give the idle task an opportunity to reclaim
the deleted 8 KiB RF stack before allocating setup resources.

A fatal console/image gate failure after capture has begun requires reset.
That path rejects another start and does not attempt AP recovery: capture may
have changed raw PHY registers whose restoration has not been validated.

## Initial AP source review

- AP configuration is zero-initialized, explicitly configured as WPA2, and
  applied before Wi-Fi starts. Local ESP-IDF 5.5 documentation says WPA2
  selects CCMP; the initially zero pairwise-cipher field is not a demonstrated
  cipher error. PMF is not required by the current configuration.
- Embedded RF availability checks have no radio side effects. The standalone
  ESP-SDR entry point is excluded, and RF hardware preparation is reached
  through capture only after SDR startup.
- Receive diagnostics forward the original buffer, length, ownership token
  and result. DHCP hooks consume no packets and add no reply options.
- The SDK AP client list reports associated stations. It contains no
  authorization/key-completion flag, so one client does not prove completion
  of the WPA handshake. Two observed EAPOL frames do not identify its completed
  stage either.

The earlier idle-AP failure with zero netif/IP/DHCP arrivals remains unresolved.
The startup correction addresses the separately reproduced error path.
Actual authentication-completion and key-install results are useful next
ESP32 observations; credentials and station identifiers need not be logged.

## Local validation

```text
cmake -S firmware/host_tests -B build/host-tests
cmake --build build/host-tests --config Release
ctest --test-dir build/host-tests -C Release --output-on-failure
```

The first source check passed all 11 native tests, including four startup API faults, deferred recovery,
retry gating, and a fatal post-capture gate. Existing tests also cover AP
protocol setup, receive forwarding and errors, chained/malformed DHCP packet
observation, serial status and ROM transport/storage logic.

These tests run the application C source with SDK/task substitutes. They do
not emulate the Wi-Fi PHY, closed radio driver, WPA handshake or over-air
packet delivery. No hardware test or firmware flash is part of this check.

## Normal AP operation before SDR

The normal HTTP path had additional responsiveness defects. ESP-IDF runs
ordinary URI handlers on one server task. The old upload reader retried three
five-second socket timeouts, occupying that task for about 15 seconds before
rollback and response. Browser status requests aborted after four seconds,
so a page could report interruption while Wi-Fi stayed associated.

Failed or rejected uploads returned `ESP_OK` after sending their error page.
The SDK then drained the unread body on the same server task, adding another
receive wait for a stopped sender. Positive reads also reset the timeout
counter, allowing a slow upload to occupy the worker for a long time.

The normal AP correction:

- receives uploads with a shared 30-second deadline across header, trainer,
  PRG and CHR reads, checked before and after each socket receive;
- uses a one-second receive timeout and rejects three consecutive timeouts;
- sends upload errors with `Connection: close` through the SDK error helper,
  then returns failure so the session closes without draining unread data;
- limits HTTP sessions to three with LRU purging, and bounds socket sends to
  two seconds;
- pauses browser status polls during actions, prevents duplicate button
  requests, and handles POST failures/timeouts instead of leaving them uncaught;
- bounds upload POSTs to 45 seconds and demo/SDR POSTs to 15 seconds, while
  preserving accepted SDR's intentional AP shutdown.

The 30-second limit covers body reception. Controller serialization, rollback,
flash commit and SRAM verification can add time. Successful uploads still use
the single HTTP task; concurrent pages can wait while installation completes.
Shorter socket timeouts are a client compatibility tradeoff that needs later
hardware qualification. Closing an unread request can cause a client network
error instead of delivery of its error body; the browser handles both outcomes.

Last recorded combined-firmware measurements on 2026-10-04 were 13,532 bytes
free internal heap and a 7,680-byte largest block with one client. The RF ring
reserves 192 KiB at boot, before SDR starts. The ROM image, HTTP/USB stacks and
Wi-Fi allocations consume the remaining memory. Streaming uploads reuse the
image buffer, and the audit found no application leak in ordinary page/status
GETs. The session cap mitigates traffic pressure; it does not establish the
cause of the earlier zero-RX failure, which also occurred in an ample-heap
comparison build.

Twelve additional native upload cases cover valid 16/32 KiB/trainer images,
malformed/oversized requests, disconnects, timeout recovery, controller errors,
and deadline expiry despite positive progress. Eight cases failed against the
old handler; all pass after correction. The complete native suite has 23 tests.
Seven Node VM tests exercise the actual embedded JavaScript with substituted
browser APIs:

```text
node --test firmware/host_tests/test_web_actions.js
```

These checks establish application behavior under simulated reads, time and
browser events. They do not establish AP association, DHCP delivery, physical
radio stability, or the timing of real flash/SRAM work.
