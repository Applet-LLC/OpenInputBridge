[日本語](README.md) | **English**

# OpenInputBridge

**A keyboard/mouse input interception driver for Windows. This project is a clean-room, from-scratch implementation of a kernel driver that is protocol-compatible with the user-mode API (LGPL) published by [Interception](https://github.com/oblitum/Interception).**

> ⚠️ This project is an **unofficial project, unaffiliated** with [oblitum/Interception](https://github.com/oblitum/Interception) and its author, Francisco Lopes. It does not use the name or trademark "Interception".

> Note: This is an English translation of the Japanese [README.md](README.md). The documents under `docs/` and `tests/` linked from this page are currently available in Japanese only. If the two READMEs differ, the Japanese version takes precedence.

## What is this?

First, a word about the original, Interception. Interception is a well-known Windows driver that can capture, modify, and re-inject keyboard/mouse input at the kernel level. Its user-mode C API library (`interception.h` / `interception.c`) is published under the LGPL. The kernel driver itself, which actually hooks the input, is available in source form only if you purchase a commercial license. Even then, you can modify it but cannot publish it; it remains closed.

OpenInputBridge re-implements this **kernel driver part** independently, in a clean-room manner, based only on:

- The source code of the published LGPL library (which is itself the specification of the communication protocol between the two)
- Microsoft's official public WDK samples and documentation (kbfiltr / moufiltr, etc.)

We never disassemble the proprietary driver binary, nor do we refer to its source (see [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) for details).

The goal is to let existing open-source user-mode libraries, and the applications that depend on them (such as the Interception fork of AutoHotkey), run unmodified on top of an Interception-compatible driver.

## Status

The WHQL version has been released. Please see [Releases](../../releases).
Note that the evaluation and subscription versions have a known problem, [issue 12](https://github.com/Applet-LLC/OpenInputBridge/issues/12), which prevents them from working correctly in some environments. It has not been fixed yet, so please keep this in mind.

The core features (driver, installer, code signing) are implemented, and verification on real hardware is almost complete. Notations such as "M0" and "M5" in other documents refer to the milestones below. For parts that are implemented but not yet tested on real hardware, the status column says so explicitly.

| Milestone | Description | Status |
|---|---|---|
| M0 | Driver skeleton (always creates 20 control devices) | ✅ Done, verified on real hardware |
| M1 | Filter attachment to keyboards/mice, pass-through | ✅ Done, verified on real hardware |
| M2 | Slot management, `IOCTL_GET_HARDWARE_ID` | ✅ Done, tested on real hardware (retrieval of hardware IDs for multiple keyboards/mice confirmed with `hardwareid.exe`) |
| M3 | Filter bitmask, capture queue, `IOCTL_READ`/`IOCTL_SET_EVENT` | ✅ Implemented and tested on real hardware. Real-hardware testing uncovered and fixed two bugs: a missed event clear in the capture queue, and the mouse filter matching method (overlap/superset) |
| M4 | `IOCTL_WRITE` (injecting synthetic input / releasing captured strokes) | ✅ Implemented and tested on real hardware |
| M5 | `IOCTL_SET_PRECEDENCE`/`IOCTL_GET_PRECEDENCE` (precedence hook chain) | ✅ Done, tested on real hardware. Real-hardware tests of simultaneous attachment from multiple processes, using `precedence_probe` in `tests/precedence_blackbox/`, confirmed that delivery order, chain continuation, blocking by capture, and attach-order tie-breaking at equal precedence all behave as expected. The filter bit-matching rules themselves are still awaiting verification against the original (see [docs/PROTOCOL.md](docs/PROTOCOL.md)) |
| M6 | Installer (`DiInstallDriver`/`DiUninstallDriver` + UpperFilters registration) | ✅ Done, tested on real hardware (install/uninstall/reboot cycles, service registration, and UpperFilters order confirmed) |
| M7 | Code signing | ✅ Both EV signing and WHQL signing are complete. Because HLK treated the driver as the System class (69 test targets), it was split into two drivers: keyboard (`oib_kbd.sys`) and mouse (`oib_mou.sys`) (see the 2026-08-01 entry in [`docs/DECISIONS.md`](docs/DECISIONS.md)). HLK resubmission and WHQL signing after the split are also complete (both `oib_kbd.cat` and `oib_mou.cat` confirmed to be signed by the Windows Hardware Compatibility Publisher) |
| M8 | Compatibility testing with the unmodified oblitum/Interception library and real applications | ✅ Done, verified on real hardware. [`tests/upstream_lib/`](tests/upstream_lib/) contains projects that build all samples under `third_party/interception/samples/` (`identify`/`hardwareid`/`caps2esc`/`axes`/`cadstop`/`mathpointer`/`x2y`) plus our own `identify2`. Verified on real hardware: basic behavior of M0/M2/M3/M4, real key remapping (both keyboard and mouse), broad filter matching with `INTERCEPTION_FILTER_KEY_ALL`, absolute coordinates, and high-frequency consecutive WRITEs. In particular, we confirmed that capturing Ctrl+Alt+Del below kbdclass works on Windows 11 as well (`cadstop`; this is fundamentally impossible with Win32 hooks). We also confirmed on real hardware that attaching unrelated tools (`x2y` + `cadstop`) at the same time works without interference. As a real-application check, we tested with [kanata](https://github.com/jtroo/kanata), a key remapping tool that has an Interception device driver mode, and confirmed tap-hold and key remapping. Furthermore, with [`tests/ahk_client/`](tests/ahk_client/), we ran [AutoHotInterception](https://github.com/evilC/AutoHotInterception) (v0.9.2), an Interception fork of AutoHotkey, unmodified, on a setup with two physical keyboards and two mice, and confirmed on real hardware per-device input capture, output (synthetic injection), per-device remapping (no crosstalk), and simultaneous operation of all four devices (2026-08-30). However, the two keyboards used were different models, so verification of the constraint stated in `docs/PROTOCOL.md` that "units with the same VID/PID can be distinguished only by slot number" remains a future task. Also, both AutoHotkey and AutoHotInterception in `tests/ahk_client/` are distributed for x64 only (no native ARM64 build exists), so even on an ARM64 machine the x64 process merely runs under Windows x64 emulation, and `redist\arm64\interception.dll` cannot be verified through this path. For ARM64, we therefore prepared `tests/upstream_lib/InterceptionLib_arm64.vcxproj`/`Identify2Sample_arm64.vcxproj` (an ARM64-native build reusing `identify2.cpp`), and confirmed on a real ARM64 machine that a native process loading `interception.dll` (ARM64 version) directly captures keyboard/mouse strokes correctly (2026-09-16) |
| M9 | Removing the total device limit (currently 20) (future work) | 📋 Not started. The **allocation ratio** between keyboards and mice within the 20 devices can now be changed via `KeyboardSlotCount` (2026-08-02, see [docs/DECISIONS.md](docs/DECISIONS.md)), but this is separate from M9; the total limit of 20 itself has not been removed. Of the current limit, 10 keyboards have been verified on real hardware; 10 mice have not been verified due to equipment constraints (see [docs/PROTOCOL.md](docs/PROTOCOL.md)). The simplest way to raise the total limit is to change the fixed value in the Interception header file, but clients would then have to be rebuilt. Ultimately, we think the approach will be to provide a new API for clients, have them rebuild, and use something like an operating-mode switch. |

For detailed task management, see [Issues](../../issues) / [Projects](../../projects).

**About M7 (WHQL signing)**: Microsoft allows two ways of signing a device driver: attestation signing and WHQL signing.
Attestation signing has the advantage of not requiring HLK tests, except that the driver cannot be published through Windows Update. For Windows 11 as well, there is no difference in how WHQL-signed and attestation-signed drivers are treated even when Secure Boot or Smart App Control is enabled, and the installation and driver-loading problems that affect test-signed drivers do not occur.
However, running the HLK tests has the benefit of assuring the quality of the device driver, so this project chose WHQL signing.
Obtaining WHQL signing requires running and submitting HLK tests. This driver was originally a single binary (`Class=System`) supporting both the keyboard and mouse classes. In that state, however, it turned out that the HLK would have 69 test targets, which is a huge number, so we split it into two drivers: keyboard (`oib_kbd.sys`, `Class=Keyboard`) and mouse (`oib_mou.sys`, `Class=Mouse`). With that, the number of HLK tests drops to around 20. HLK resubmission and WHQL signing for both drivers after the split are complete.

## Architecture Overview

- A filter driver based on KMDF (Kernel-Mode Driver Framework). It consists of two binaries: keyboard (`oib_kbd.sys`, `Class=Keyboard`) and mouse (`oib_mou.sys`, `Class=Mouse`), both referencing the shared logic in `driver/common/`. Native builds for both x64 and ARM64 are included in the distribution zip, and the one matching the host architecture is selected automatically at install time. The installer itself (`OpenInputBridgeSetup.exe`/`OpenInputBridgeSetup-arm64.exe`) also needs native builds for x64 and ARM64 (the driver installation call itself, `SetupInstallServicesFromInfSectionW`, rejects calls from a process whose architecture differs from the host, so the x64 version cannot be run under emulation on an ARM64 machine. See the ARM64 entry in [docs/DECISIONS.md](docs/DECISIONS.md) for details). The toast helper (`OibToastHelper.exe`) does not call driver installation APIs, so it is x64 only and runs under Windows x64 emulation on ARM64. The plain names `keyboard.sys`/`mouse.sys` are not used because they would collide in the Driver Store with the `keyboard.inf`/`mouse.inf` (in-box drivers for PS/2 keyboards/mice) shipped with Windows (see the 2026-08-02 entry in [docs/DECISIONS.md](docs/DECISIONS.md))
- Each attaches as an upper filter to the corresponding device class stack, and captures/re-injects input by replacing the class service callback via `IOCTL_INTERNAL_*_CONNECT`
- Interception requires installing both the keyboard and mouse drivers, and we follow that.
- It always exposes 20 control devices regardless of whether physical devices exist, to maintain compatibility with upper-layer user-mode libraries. By default there are 10 keyboards and 10 mice, but OpenInputBridge lets you change this allocation with the `KeyboardSlotCount` registry value (the installer's `--slots=N`) (see [Installation](#installation) and [`docs/PROTOCOL.md`](docs/PROTOCOL.md))
- To let new clients check the actual allocation and identify OpenInputBridge itself, we also provide custom extension IOCTLs, `IOCTL_GET_KEYBOARD_SLOT_COUNT`/`IOCTL_GET_DRIVER_IDENTITY` (see [`docs/PROTOCOL.md`](docs/PROTOCOL.md))
- See [`docs/PROTOCOL.md`](docs/PROTOCOL.md) for the detailed protocol specification
- For compatibility, the limit is for now 20 devices in total, as specified by the oblitum/Interception library (depending on the allocation above, up to 20 keyboards or 20 mice). We plan an extension to remove this total limit in the future. Because the count is hard-coded in the header files of existing clients, it cannot be done while preserving backward compatibility, and we expect some measure such as switching operating modes will be needed.

## License and Distribution Policy

This repository is **fully open source**.

| Deliverable | License / Price |
|---|---|
| Complete source code (this repository) | [MIT License](LICENSE), free |
| Self-built binaries (run in test-signing mode) | Free; build at your own risk |
| WHQL-signed driver | **Paid** (details to be announced soon) |

Anyone can freely read and modify the source code, and build and use it themselves. On the other hand, the **WHQL-signed device driver**, which lets ordinary users install a Windows kernel driver easily and safely, is offered for a fee to cover the costs of obtaining certificates, certification, and ongoing support. Note that this project does not provide self-built binaries.

The user-mode library of oblitum/Interception incorporated in `third_party/interception/` (`interception.c` / `interception.h`) remains unmodified and keeps its original LGPL-3.0 license. The EV-signed `interception.dll` (included in the `redist\` folder of the distribution zip, see below) was built from [Applet-LLC/Interception](https://github.com/Applet-LLC/Interception), a public fork of oblitum/Interception with unmodified contents.

## Installation

When you extract the zip (`OpenInputBridge.zip`) produced by building the solution's `Packaging` project, it contains `oib_kbd\` and `oib_mou\` (each with `.inf`/`.cat`/`.sys` under `x64\` and `arm64\` subfolders), plus
`OpenInputBridgeSetup.exe` (x64), `OpenInputBridgeSetup-arm64.exe` (ARM64), and `setup.bat`. `setup.bat` automatically determines which `OpenInputBridgeSetup*.exe` to use based on the host architecture.

**About the client-side `interception.dll`**: For convenience, this zip includes an EV-signed Interception-compatible client library as `redist\x64\interception.dll` / `redist\arm64\interception.dll` (the driver and the installer themselves never load or verify this DLL; it is merely bundled). It is built from [Applet-LLC/Interception](https://github.com/Applet-LLC/Interception) (a public fork of oblitum/Interception; the library itself is unmodified) and is licensed under LGPL-3.0. See the bundled `redist\THIRD-PARTY-NOTICES.txt`/`redist\LGPL-3.0.txt` for details and the full license text. You are free to replace it with one you built yourself (`third_party/interception/` in this repository is the unmodified source of this library, and interception.dll can be built from it; [`tests/upstream_lib/`](tests/upstream_lib/) does exactly this).

**Prerequisites (for test signing / EV signing)**: If the driver is signed with a test signature, or with an EV signature (before WHQL was obtained),
run the following in a Command Prompt with administrator privileges before installing, and then reboot.

```bat
bcdedit /set TESTSIGNING ON
```

If you use Secure Boot or BitLocker, you need to turn off BitLocker in Windows settings and
disable Secure Boot on the BIOS screen. If you disable Secure Boot first by mistake,
you will be asked for the BitLocker key at startup.

Also, when Smart App Control is enabled, drivers that are not WHQL-certified are blocked from loading
even if test signing is enabled
(Device Manager shows "Code 39", and Event Viewer shows event IDs 3077/3004 under
"Applications and Services Logs" → "Microsoft" → "Windows" → "CodeIntegrity" →
"Operational"). Turn it off from Settings → "Privacy & security" → "Windows Security" →
"App & browser control". **Once turned off, it cannot be turned back on until you reinstall or reset Windows.** Note in particular that it is often on by default on fresh Windows 11 installations, including recent ARM64 machines.

None of these steps is necessary for WHQL-signed drivers.

### Installing with `setup.bat`

- Running setup.bat performs the installation. The following is what the batch file does.

- It checks that the machine is x64 or ARM64 and Windows 11 or later (Windows 10 is **not supported**, even the latest 22H2.
  If installed, the keyboard and mouse become unusable after a reboot). If the check fails,
  it displays "This is the wrong Windows version. It's for Windows 11." and aborts on the spot
- If the conditions are met, it elevates itself to administrator privileges (since a .bat file itself
  has no manifest that triggers UAC automatically) and installs both the keyboard and mouse drivers
- It enables the [audit log and notification features](#audit-log-and-notification-features) by default (internally, it is just a simple
  wrapper that runs `OpenInputBridgeSetup.exe --enable-audit-log` and `--enable-toast` in turn)
- Finally, it checks the installation result with `--verify-install` (if a filter registration exists but the actual
  driver files cannot be found, the registration is removed automatically; if the audit log and notification features
  have not been enabled, a message to that effect is displayed)

If you do not need the audit log and notification features, you can disable them after installation with `OpenInputBridgeSetup.exe --disable-audit-log` and
`--disable-toast`. If you want to install only one of the drivers, or change the
keyboard/mouse allocation with `--slots=N`, do not use `setup.bat`; instead run
`OpenInputBridgeSetup.exe` directly with the necessary arguments, as described below.

`OpenInputBridgeSetup.exe` links the CRT statically, so there is no need to install the Visual C++
Redistributable separately.

**On ARM64**: Everywhere this document says `OpenInputBridgeSetup.exe`,
read it as the bundled `OpenInputBridgeSetup-arm64.exe` (arguments and behavior are identical).
If you run the x64 version as-is on ARM64, the driver installation stops partway with a
`SetupInstallServicesFromInfSectionW` failure (`ERROR_IN_WOW64`)
(see [docs/DECISIONS.md](docs/DECISIONS.md) for details). Installation via `setup.bat`
detects this automatically, so you only need to be aware of this substitution when running `OpenInputBridgeSetup*.exe` directly.

When finished, **please reboot.** `UpperFilters` (the filter registration for device classes) takes effect only
when the device stack is built at OS startup, so it does not become active without a reboot.

After rebooting, you can verify the following from a Command Prompt with administrator privileges (running the `sc` command itself requires
administrator privileges).

- `sc query OpenInputBridgeKeyboard` / `sc query OpenInputBridgeMouse`: TYPE should be
  `KERNEL_DRIVER` and STATE should be `RUNNING`
- `sc config ... start=` only changes the start type for the next boot; it does not affect the current running state
- `sc stop` is also ineffective for these services (error 1052). You cannot change the state of a running driver
  directly from `sc`

When you run `OpenInputBridgeSetup.exe` directly, it has a manifest requiring administrator privileges,
so a UAC elevation prompt appears automatically. With no arguments, both the keyboard and mouse drivers
are installed (specifying `keyboard`/`mouse` as an argument allows installing just one, but use this only when running HLK. By design, both drivers must be installed for it to work.). The Windows
version/architecture check described above is performed by `OpenInputBridgeSetup.exe`
itself on every run, whether or not `setup.bat` is used (it can be disabled with `--skip-version-check`, but this is intended for cases such as
the dedicated installers of the Pro/Subscription editions performing an equivalent check themselves, and is normally not needed).

### Uninstallation

If you enabled the audit log and toast notification features, also run `OpenInputBridgeSetup.exe --disable-audit-log` and `OpenInputBridgeSetup.exe --disable-toast` from a Command Prompt with administrator privileges. This removes the Task Scheduler tasks.

To uninstall the device drivers, run `OpenInputBridgeSetup.exe /uninstall` with administrator privileges and reboot.

With no arguments, both are uninstalled; specify `keyboard`/`mouse` to uninstall just one. Note that uninstalling only one leaves an abnormal state, and by design it will not work correctly. This is for use when both were installed by mistake while running HLK.

**Never use `pnputil -d oemXX.inf`.** It leaves behind information that is not in the inf but was set by the installer, and the keyboard and mouse become unusable at the next reboot.

### Changing the keyboard/mouse allocation at install time

By default, the 20 devices `\\.\interception00` to `19` are divided evenly into
10 keyboards and 10 mice, but you can change this allocation with `--slots=N` (used together with `keyboard`/`mouse`,
at install time only).

For example, `OpenInputBridgeSetup.exe install
keyboard --slots=15` gives an allocation of 15 keyboards and 5 mice, and if the other driver is
already installed, its allocation follows automatically.

This allocation differs from the fixed 10/10 split assumed by the original Interception
library, so **old clients (that do not know about this specification) will not work correctly with any allocation other than the default** (they will not crash,
but will treat the range with the shifted boundary as the wrong device type, resulting in silent failures such as being unable to capture or misinterpreting data). See
[`docs/PROTOCOL.md`](docs/PROTOCOL.md) for details.


## Audit Log and Notification Features

`OpenInputBridgeSetup.exe` provides an audit log feature that records processes that open an Interception-protocol-compatible control device
(`\\.\interceptionNN`) in the standard Windows Security event log, and a notification feature that alerts you to such events in real time with toast notifications
(both are implemented using only standard OS features, without modifying the device driver itself). When installed via `setup.bat`,
both are enabled by default. For design decisions, see
[`docs/DECISIONS.md`](docs/DECISIONS.md); for known limitations and the alternatives considered, see
[`docs/SECURITY_CONSIDERATIONS.md`](docs/SECURITY_CONSIDERATIONS.md).

**Important**: These features work by registering the full path of `OpenInputBridgeSetup.exe` in the extracted location
in Task Scheduler tasks. Therefore, **do not delete or move the folder where you extracted the zip file, even after installation**. The extracted location effectively becomes the install location.
**Do not extract it under a cloud-synced folder such as OneDrive (including a synced "Desktop"
folder)**. The `OpenInputBridgeAuditLogReapply` task, which reapplies the audit log SACL at every startup, runs with SYSTEM privileges at boot (before the user's sign-in
session is established), but on real hardware we confirmed that when installed under a OneDrive-synced folder, this reapplication does not run
(apparently affected by placeholder files from Files On-Demand and the like), and as a result the audit log and toast notification features do not work at all.
We recommend extracting to a local path that is not cloud-synced, such as `C:\OpenInputBridge`.

**How to check the audit log**: You can check in Event Viewer whether the audit log is actually being recorded.
Open "Event Viewer → Windows Logs → Security", look for events with event ID **4656** whose
`ObjectName` is `\Device\interceptionNN` (`NN` is a number from 00 to 19), and check whether
the `Process Name` is the process you expect. If a process name you do not recognize
is recorded, it means that process is actually in a position to
observe and inject Interception-protocol-compatible key/mouse input.

**How to use toast notifications**: A toast notification shows the name of the process that opened the device driver. If the process name is unknown, it shows Unknown; if known, clicking the toast notification opens, in Explorer, the folder containing that process (executable). This lets you check whether the executable was launched intentionally. If it is an executable you do not recognize, it has only just opened the device, so it has barely been able to watch your keyboard or mouse operations. We recommend ending that process from Task Manager, and uninstalling the program if it is unwanted.

Run the following from a Command Prompt with administrator privileges.

```bat
:: Enable / disable the audit log
OpenInputBridgeSetup.exe --enable-audit-log
OpenInputBridgeSetup.exe --disable-audit-log

:: Enable / disable toast notifications (enabling requires the audit log to be enabled)
OpenInputBridgeSetup.exe --enable-toast
OpenInputBridgeSetup.exe --disable-toast

:: For trusted processes, you can suppress only the notification (the audit log itself still records everything)
OpenInputBridgeSetup.exe --allow-process "C:\full\path\to\app.exe"
OpenInputBridgeSetup.exe --disallow-process "C:\full\path\to\app.exe"
OpenInputBridgeSetup.exe --list-allowed-processes

:: Check the state after installation (setup.bat runs this automatically at the end)
OpenInputBridgeSetup.exe --verify-install
```

## Building

### Getting the repository

`third_party/interception` is a git submodule that incorporates the `library/` folder of [oblitum/Interception](https://github.com/oblitum/Interception) unmodified. **Be sure to fetch the submodule too.**

```sh
git clone --recurse-submodules https://github.com/Applet-LLC/OpenInputBridge.git
```

If you have already done a regular `git clone`, you can fetch the submodule with:

```sh
git submodule update --init --recursive
```

### Build (WDK required, `OpenInputBridge.sln`)

`driver/` is a Windows kernel driver based on KMDF (Kernel-Mode Driver Framework). The following are required to build it.

- Visual Studio 2022 or later
- Windows Driver Kit (WDK) or EWDK (Enterprise WDK, a standalone version used by mounting an ISO image)

`OpenInputBridge.sln` at the repository root bundles three projects: driver, installer, and signing/packaging.
Everything from building the driver/installer to EV signing, collecting into `Signed\`, and creating the distribution zip
(`dist\OpenInputBridge.zip`) can be processed in one go just by switching the solution configuration.

The driver (`oib_kbd`/`oib_mou`) and the installer (`OpenInputBridgeSetup`) both
produce x64 and ARM64 builds in a single solution build. The ARM64 versions exist as separate projects whose Platform is fixed to ARM64 in Configuration Manager (`oib_kbd_arm64`/`oib_mou_arm64`/
`OpenInputBridgeSetup_arm64`, sharing sources with their x64 counterparts), so building with the usual `Platform=x64` builds the ARM64 versions as well, and there is no need to
switch platforms manually. The installer needs a native ARM64 build as well because
the driver's service registration (`SetupInstallServicesFromInfSectionW`) rejects calls from a process of a different
architecture than the host (confirmed on real hardware; see
[docs/DECISIONS.md](docs/DECISIONS.md) for details), so the x64 version cannot be
run under emulation on ARM64. The toast helper (`OibToastHelper.exe`) does not call driver installation APIs,
so it is x64 only and runs under Windows x64 emulation on ARM64.
To cross-build for ARM64, the ARM64 build tools must be installed in the Visual Studio/WDK environment you use.

| Solution configuration | Description |
|---|---|
| `Debug` | Builds only the driver and installer (test signing, for development). Signing and packaging are not run. **Use this if you build and try it yourself** (corresponds to "Self-built binaries" in the table above) |
| `Release` | Builds the driver and installer, signs both with this project's EV certificate, collects them into `packaging\Signed\`, and creates `dist\OpenInputBridge.zip` (the distribution before WHQL signing was obtained) |
| `ReleaseWHQL` | EV-signs only the installer and collects it; creates the zip while leaving the files returned from the HLK/WHQL submission, **manually placed and never overwritten**, in `packaging\Signed\oib_kbd\x64\`, `packaging\Signed\oib_kbd\arm64\`, `packaging\Signed\oib_mou\x64\`, and `packaging\Signed\oib_mou\arm64\` (the distribution after WHQL signing was obtained. WHQL certification requires a separate HLK submission for each architecture) |

`Release`/`ReleaseWHQL` assume an EV certificate in the name of this project (Applet LLC), and will not complete as intended in anyone else's environment. If you use `Release`/`ReleaseWHQL` in an environment without the certificate, the signing command in `packaging\sign.mak` fails, but it is built to ignore the failure, so **the build itself finishes normally and unsigned binaries (not even test-signed) end up in `dist\OpenInputBridge.zip`**. Binaries in this state cannot be loaded even on Windows with test signing enabled. If you do not have an EV certificate, be sure to use the `Debug` configuration.

When using the EWDK, you can build the entire solution as-is from a Command Prompt with the build environment set up. If you build it yourself (without an EV certificate), specify the `Debug` configuration.

```bat
K:\BuildEnv\SetupBuildEnv.cmd amd64
msbuild OpenInputBridge.sln /p:Configuration=Debug /p:Platform=x64
```

You can also open the Visual Studio IDE with `devenv OpenInputBridge.sln` while keeping the same environment variables.

Before using the `ReleaseWHQL` configuration, manually copy the `oib_kbd.inf`/`oib_kbd.cat`/`oib_kbd.sys` returned from the x64 and ARM64 HLK/WHQL submissions (they are separate submissions) to `packaging\Signed\oib_kbd\x64\` and
`packaging\Signed\oib_kbd\arm64\`, and `oib_mou.inf`/`oib_mou.cat`/`oib_mou.sys` to
`packaging\Signed\oib_mou\x64\` and `packaging\Signed\oib_mou\arm64\`
(`Signed\Symbol\` is updated automatically from our local build every time).

For the internal processing of signing and packaging (individual nmake targets, etc.), see the comments in `packaging/sign.mak`.
For the roadmap, see [Status](#status).

### Verification (`tests/upstream_lib/`, `tests/precedence_blackbox/`)

The unmodified `third_party/interception/library/interception.c`, all of its samples
(`identify`/`hardwareid`/`caps2esc`/`axes`/`cadstop`/`mathpointer`/`x2y`), and our own
`identify2` and `precedence_probe` for real-hardware testing are also
built in the Debug/Release configurations of the same `OpenInputBridge.sln`. Against an installed OpenInputBridge, you can verify at hand not only keyboard/mouse capture and
pass-through, but also real key remapping, the precedence hook chain, and Ctrl+Alt+Del capture.
See [`tests/upstream_lib/README.md`](tests/upstream_lib/README.md) and
[`tests/precedence_blackbox/README.md`](tests/precedence_blackbox/README.md) for details (Japanese only).

## Contributing

Issues and Pull Requests are welcome. Because this is a security-sensitive area, a kernel driver project, reviews may take time depending on the changes.

## Clean-Room Policy

This project is implemented by referring only to the following:

- The LGPL-published library source code of Interception (the primary source for the protocol)
- Microsoft's official WDK/KMDF documentation and public sample drivers
- Black-box observation of the I/O behavior of the original driver (no disassembly or decompilation whatsoever)

Detailed evidence is recorded in [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md).

## Notes on Use

By its nature, an Interception-protocol-compatible driver allows, once installed, even ordinary user
processes without administrator privileges to observe and inject system-wide keyboard/mouse input (this is an intentional specification for protocol compatibility, the same as the original Interception).
This means it can also be used in ways that can be abused, such as unauthorized access, cheating, and
monitoring without the person's consent (keylogger-like use).

Do not use this software for unauthorized access to third parties, for cheating in online games or circumventing anti-cheat
mechanisms, or for monitoring or recording without the person's consent.

Detailed findings on technical detectability and abuse risk, along with countermeasure ideas that have not yet been addressed, are
summarized in [`docs/SECURITY_CONSIDERATIONS.md`](docs/SECURITY_CONSIDERATIONS.md).

## Disclaimer

As stated in the MIT License as well, even if any disadvantage or accident occurs from using the source code of this software or the binaries generated from it (including the EV-signed version, and the Subscription and Pro versions that include the WHQL-signed version), the responsibility lies not with the developer or the seller but with the user. For example, even if this driver is obstructed by other antivirus or anti-cheat software, we will make our best effort, but cannot promise any improvement in response. If you encounter a malfunction caused by a bug or the like, we will sincerely try to fix it if it is a defect that can be improved.

## License

The original parts are under the [MIT License](LICENSE). Everything under `third_party/interception/` follows the LGPL-3.0 of the oblitum/Interception library. The `redist\interception.dll` (x64/ARM64) included in the distribution zip is likewise LGPL-3.0, and was built from [Applet-LLC/Interception](https://github.com/Applet-LLC/Interception) (a public fork of oblitum/Interception). See the bundled `redist\THIRD-PARTY-NOTICES.txt` for details.
