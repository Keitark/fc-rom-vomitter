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

All 11 native tests pass, including four startup API faults, deferred recovery,
retry gating, and a fatal post-capture gate. Existing tests also cover AP
protocol setup, receive forwarding and errors, chained/malformed DHCP packet
observation, serial status and ROM transport/storage logic.

These tests run the application C source with SDK/task substitutes. They do
not emulate the Wi-Fi PHY, closed radio driver, WPA handshake or over-air
packet delivery. No hardware test or firmware flash is part of this check.
