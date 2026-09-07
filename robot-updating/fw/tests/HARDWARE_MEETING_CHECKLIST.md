# StackChan Meeting Hardware Verification Checklist

Date: 2026-07-07
Branch: `feature/meeting-backend-task1`
Status: BLOCKED - ESP-IDF is not available in the current shell, so hardware flashing and monitor validation were not run.

## Environment Evidence

- `idf.py`: not found in `PATH`.
- `IDF_PATH`, `ESP_IDF_VERSION`, `IDF_TOOLS_PATH`: not set in the current shell.
- Checked common ESP-IDF install locations:
  - `C:\Espressif`: not found or not accessible.
  - `C:\Users\15286\esp`: not found or not accessible.
  - `D:\Espressif`: not found or not accessible.
- Serial port names visible through .NET: `COM5`, `COM7`.
- WMI serial-port metadata query timed out after an elevated attempt, so the StackChan device port was not positively identified.

## Pre-Hardware Regression Evidence

- Host test `test_meeting.exe`: PASS.
- Host test `motion_math_test.exe`: PASS.
- Both host test binaries were compiled with `g++ -std=gnu++17 -Wall -Wextra -Werror`.

## Required Hardware Checks

Use this checklist only after ESP-IDF is configured and exactly one StackChan USB serial device is connected.

### 1. Flash And Launcher Regression

- [ ] Run `cd firmware; idf.py flash monitor` with ESP-IDF port auto-detection.
- [ ] Confirm the Meeting app appears in the launcher.
- [ ] Confirm all original launcher icons still open:
  - [ ] AI Agent
  - [ ] Avatar
  - [ ] ESP-NOW Control
  - [ ] App Center
  - [ ] EzData
  - [ ] Dance
  - [ ] Setup

Evidence:

```text
Pending.
```

### 2. Ten-Minute Recording

- [ ] Record for 10 minutes.
- [ ] Verify two finalized five-minute WAV files exist.
- [ ] Verify both WAV files are readable.
- [ ] Verify sample continuity across the segment boundary.

Expected PCM payload for 10 minutes at 16 kHz mono 16-bit: about 19.2 MB.

Evidence:

```text
Pending.
```

### 3. Network Fault And Resume

- [ ] Start a meeting recording.
- [ ] Disable Wi-Fi for 3 minutes.
- [ ] Confirm recording continues locally while offline.
- [ ] Re-enable Wi-Fi.
- [ ] Confirm missing audio is uploaded after reconnect.
- [ ] Confirm manifest `lastAckSequence` advances after ACKs.

Evidence:

```text
Pending.
```

### 4. Reboot During `.part`

- [ ] Start recording.
- [ ] Reboot while the active segment is still `.part`.
- [ ] Confirm recovery detects the `.part` file.
- [ ] Confirm a valid `.wav` is published after recovery.
- [ ] Confirm manifest resume does not skip or duplicate samples.

Evidence:

```text
Pending.
```

### 5. Two-Hour Soak

- [ ] Run a two-hour powered recording test.
- [ ] Verify about 230.4 MB PCM payload is produced.
- [ ] Confirm no watchdog reset.
- [ ] Confirm no capture queue overrun that affects SD recording.
- [ ] Confirm final WAV readability.

Evidence:

```text
Pending.
```

### 6. Exit And Original Feature Regression

- [ ] Exit Meeting app.
- [ ] Confirm microphone is released and AI Agent can record normally.
- [ ] Confirm Avatar still opens and animates.
- [ ] Confirm Dance still opens and responds.
- [ ] Confirm remote control path still works.
- [ ] Confirm Setup still opens.
- [ ] Confirm App Center still opens.
- [ ] Confirm OTA flow still opens.

Evidence:

```text
Pending.
```

## Completion Criteria

Task 6 can be marked complete only after every required hardware check above has passing evidence from a real StackChan device.
