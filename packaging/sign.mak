# Copyright (c) 2026 OpenInputBridge Contributors
# SPDX-License-Identifier: MIT
# Licensed under the MIT License. See LICENSE file in the project root for full license text.
#
# EV code-signing + distribution packaging for Release binaries (M7). The signing part mirrors
# the Applet LLC "nodoka" project's own d.sign/d.sign.mak (private signing certificate, not
# part of this public repo).
#
# Signs directly (no signature-stripping pass): nothing here ships a pre-signed third-party
# binary, so there's no existing signature to remove before applying ours. The driver .sys files
# are built with $(SignMode)=Off for Release (see driver/keyboard/oib_kbd.vcxproj and
# driver/mouse/oib_mou.vcxproj) specifically so they arrive here unsigned — no leftover WDK test
# signature to worry about, unlike Debug builds.
#
# Normally invoked through ..\OpenInputBridge.sln's "Packaging" project (a Makefile-type
# project — see Packaging.vcxproj), which runs this with the right target list per solution
# configuration (Release -> "all package", ReleaseWHQL -> "whql package"). Called directly:
#   nmake -f packaging\sign.mak            (sign, then stage into Signed\ — everything, EV-only)
#   nmake -f packaging\sign.mak sign       (sign only: installer .exe + both driver .sys/.cat)
#   nmake -f packaging\sign.mak sign-bin   (sign only: installer .exe + toast helper .exe)
#   nmake -f packaging\sign.mak sign-driver (sign only: both drivers' .sys/.cat)
#   nmake -f packaging\sign.mak stage      (stage only, no signing)
#   nmake -f packaging\sign.mak whql       (post-WHQL: sign+stage installer .exe and refresh
#                                            Symbol\, WITHOUT touching Signed\oib_kbd\/oib_mou\)
#   nmake -f packaging\sign.mak package    (zip whatever's currently in Signed\ into dist\)
#
# Since docs/DECISIONS.md's 2026-07-30 entry, this ships TWO independent driver packages
# (oib_kbd.sys/Class=Keyboard and oib_mou.sys/Class=Mouse — see driver/keyboard/, driver/mouse/)
# instead of the original single OpenInputBridge.sys/Class=System binary. Every target below
# that used to touch one driver package now touches both. Package base names are
# "oib_kbd"/"oib_mou" rather than the more obvious "keyboard"/"mouse" to avoid colliding with
# the inbox keyboard.inf/mouse.inf every Windows install already has in the Driver Store — see
# driver/keyboard/oib_kbd.inx's header comment and docs/DECISIONS.md's 2026-08-02 entry
# (second one).
#
# Two distinct pipelines into Signed\, both ending in the same "package" step:
#
#   Pre-WHQL ("all"): these drivers haven't been through HLK/WHQL yet, so their .sys/.cat get
#   our own plain EV signature (signtool, our own certificate) same as the installer .exe -- not
#   Microsoft attestation signing, which Microsoft no longer offers as a standalone option;
#   getting a WHQL-trusted catalog now means an actual HLK submission (see README.md's M7
#   status note). "all" = sign + stage covers all of Signed\ from our own Release build output.
#
#   Post-WHQL ("whql"): once these drivers have gone through HLK/WHQL, their .inf/.cat/.sys no
#   longer come from local signing — they come back from that submission process and get
#   dropped into Signed\oib_kbd\x64\, Signed\oib_kbd\arm64\, Signed\oib_mou\x64\, and
#   Signed\oib_mou\arm64\ BY HAND (this makefile never writes there in this mode). WHQL
#   certification is architecture-specific, so ARM64 goes through its own independent HLK
#   submission, separate from x64's — the two land in their own arch subfolders so neither
#   submission's returned files overwrite the other's. Only the installer .exe (never part of
#   the WHQL submission) still needs local EV signing. WHQL doesn't hand symbols back, so
#   Signed\Symbol\ still needs our own .pdb — but "our own" must mean the exact build that was
#   actually submitted, not whatever happens to be sitting in the local Release output right
#   now: if the driver gets rebuilt (even a no-op rebuild) between submission and running this
#   target, a .pdb pulled fresh from the build tree would silently no longer match the signed
#   .sys HLK returned. To avoid that, drop the submission-time .pdb into
#   Signed\oib_kbd\/oib_mou\ BY HAND alongside the returned .inf/.cat/.sys (see
#   TARGET_PDB_KBD/MOU and their _ARM64 counterparts below) — it's used in preference to the
#   local build output when present. "whql" therefore only ever touches the installer .exe and
#   Symbol\, and leaves Signed\oib_kbd\/oib_mou\ exactly as you placed them.
#
# "package" is deliberately independent of both "all" and "whql": it only ever looks at
# what's currently sitting in Signed\, so the same single step works regardless of which
# pipeline (or manual copy) put it there.
#
# No GUI installer for this repo (see docs/DECISIONS.md's 2026-08-09 entry): OpenInputBridgeSetup.exe
# itself is the only installer OSS users get — README.md/LICENSE are bundled alongside it (see
# stage-bin) so the zip is self-explanatory on its own, and --enable-toast (installer/
# toastsetup.cpp) creates its own Start Menu shortcut directly via IShellLink rather than relying
# on a WiX-authored one. The sibling Pro/Subscription repos keep their own WiX MSI installers
# (needed there for license-key entry UI), which vendor/rebuild from this same Signed\ output.
#
# Prerequisite: both drivers (x64 AND ARM64 — driver/keyboard/oib_kbd.vcxproj +
# oib_kbd_arm64.vcxproj, driver/mouse/oib_mou.vcxproj + oib_mou_arm64.vcxproj) and the x64-only
# installer must already be built (see ..\OpenInputBridge.sln, or README.md's ビルド方法 for
# standalone project builds — Configuration Manager pins the two ARM64 driver projects to
# Platform=ARM64 under every existing solution configuration, so an ordinary solution build
# already produces both architectures with no extra step). Each driver's Inf2Cat step produces
# its own architecture's package folder this reads from:
#   ..\driver\keyboard\x64\Release\oib_kbd\{oib_kbd.inf, oib_kbd.cat, oib_kbd.sys}
#   ..\driver\keyboard\ARM64\Release\oib_kbd\{oib_kbd.inf, oib_kbd.cat, oib_kbd.sys}
#   ..\driver\mouse\x64\Release\oib_mou\{oib_mou.inf, oib_mou.cat, oib_mou.sys}
#   ..\driver\mouse\ARM64\Release\oib_mou\{oib_mou.inf, oib_mou.cat, oib_mou.sys}
#
# Neither Signed\ nor dist\ are checked into git (build/signing output, not source — see
# .gitignore). Signed\ is not cleaned by a solution "Clean" of the ReleaseWHQL configuration
# (see Packaging.vcxproj) since it can hold hand-placed WHQL submission output that took days
# to get back — only dist\ is safe to blow away and regenerate on every build.
#
# Distribution layout (matches what installer/install.cpp's InfPath lookup expects —
# <exeDir>\<PackageName>\<arch>\<PackageName>.inf per driver, <arch> being "x64" or "arm64"
# per common.h's IsNativeArm64() — a single zip carries both architectures, and the installer
# picks the right one at install time based on the host's native architecture):
#   setup.bat
#   OpenInputBridgeSetup.exe
#   OpenInputBridgeSetup-arm64.exe
#   OibToastHelper.exe
#   OibToastHelper.ico
#   README.md
#   LICENSE
#   README.ja-JP.txt
#   README.en-US.txt
#   oib_kbd\x64\oib_kbd.inf
#   oib_kbd\x64\oib_kbd.cat
#   oib_kbd\x64\oib_kbd.sys
#   oib_kbd\arm64\oib_kbd.inf
#   oib_kbd\arm64\oib_kbd.cat
#   oib_kbd\arm64\oib_kbd.sys
#   oib_mou\x64\oib_mou.inf
#   oib_mou\x64\oib_mou.cat
#   oib_mou\x64\oib_mou.sys
#   oib_mou\arm64\oib_mou.inf
#   oib_mou\arm64\oib_mou.cat
#   oib_mou\arm64\oib_mou.sys
#   Symbol\oib_kbd.pdb
#   Symbol\oib_kbd_arm64.pdb
#   Symbol\oib_mou.pdb
#   Symbol\oib_mou_arm64.pdb

DRIVER_PACKAGE_DIR_KBD	= ..\driver\keyboard\x64\Release\oib_kbd

DRIVER_PACKAGE_DIR_KBD_ARM64	= ..\driver\keyboard\ARM64\Release\oib_kbd

DRIVER_PACKAGE_DIR_MOU	= ..\driver\mouse\x64\Release\oib_mou

DRIVER_PACKAGE_DIR_MOU_ARM64	= ..\driver\mouse\ARM64\Release\oib_mou

TARGET_SYS_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.sys

TARGET_INF_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.inf

TARGET_CAT_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.cat

TARGET_SYS_KBD_ARM64	= $(DRIVER_PACKAGE_DIR_KBD_ARM64)\oib_kbd.sys

TARGET_INF_KBD_ARM64	= $(DRIVER_PACKAGE_DIR_KBD_ARM64)\oib_kbd.inf

TARGET_CAT_KBD_ARM64	= $(DRIVER_PACKAGE_DIR_KBD_ARM64)\oib_kbd.cat

# Prefers a hand-placed, submission-matching .pdb (Signed\oib_kbd\x64\oib_kbd.pdb, dropped in
# next to the WHQL-returned .inf/.cat/.sys) over the local Release build output, since the
# latter can silently drift out of sync with what was actually submitted (see header comment
# above).
!IF EXIST(Signed\oib_kbd\x64\oib_kbd.pdb)
TARGET_PDB_KBD	= Signed\oib_kbd\x64\oib_kbd.pdb
!ELSE
TARGET_PDB_KBD	= ..\driver\keyboard\x64\Release\oib_kbd.pdb
!ENDIF

!IF EXIST(Signed\oib_kbd\arm64\oib_kbd.pdb)
TARGET_PDB_KBD_ARM64	= Signed\oib_kbd\arm64\oib_kbd.pdb
!ELSE
TARGET_PDB_KBD_ARM64	= ..\driver\keyboard\ARM64\Release\oib_kbd.pdb
!ENDIF

TARGET_SYS_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.sys

TARGET_INF_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.inf

TARGET_CAT_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.cat

TARGET_SYS_MOU_ARM64	= $(DRIVER_PACKAGE_DIR_MOU_ARM64)\oib_mou.sys

TARGET_INF_MOU_ARM64	= $(DRIVER_PACKAGE_DIR_MOU_ARM64)\oib_mou.inf

TARGET_CAT_MOU_ARM64	= $(DRIVER_PACKAGE_DIR_MOU_ARM64)\oib_mou.cat

!IF EXIST(Signed\oib_mou\x64\oib_mou.pdb)
TARGET_PDB_MOU	= Signed\oib_mou\x64\oib_mou.pdb
!ELSE
TARGET_PDB_MOU	= ..\driver\mouse\x64\Release\oib_mou.pdb
!ENDIF

!IF EXIST(Signed\oib_mou\arm64\oib_mou.pdb)
TARGET_PDB_MOU_ARM64	= Signed\oib_mou\arm64\oib_mou.pdb
!ELSE
TARGET_PDB_MOU_ARM64	= ..\driver\mouse\ARM64\Release\oib_mou.pdb
!ENDIF

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

# Native ARM64 installer (installer/OpenInputBridgeSetup_arm64.vcxproj) — see its header comment
# for why this can't just be the x64 exe running under emulation: SetupInstallServicesFromInfSectionW
# (install.cpp) refuses to run from a non-native process (ERROR_IN_WOW64), confirmed on real
# ARM64 hardware. Ships flat alongside TARGET_BIN under a different name; setup.bat picks the
# one matching the host's native architecture.
TARGET_BIN_ARM64	= ..\installer\ARM64\Release\OpenInputBridgeSetup-arm64.exe

# --enable-toast's Scheduled Task (installer/toastsetup.cpp) launches this; it must ship
# alongside OpenInputBridgeSetup.exe the same way the driver packages do.
TARGET_TOAST_HELPER	= ..\installer\toast-helper\x64\Release\OibToastHelper.exe

# Loose copy of the same icon embedded in OibToastHelper.exe (installer/toast-helper/
# OibToastHelper.rc) — toastsetup.cpp's RegisterAumid() needs a direct file path for the
# AUMID's IconUri, not an embedded exe resource. Not a build output — copied straight from
# source, so it's read from installer/toast-helper/ directly rather than an x64\Release\ dir.
TARGET_TOAST_ICON	= ..\installer\toast-helper\OibToastHelper.ico

# Not build outputs — copied straight from the repo root so end users unzipping the release
# have README/LICENSE on hand without needing to visit the GitHub repo separately.
TARGET_README	= ..\README.md

TARGET_LICENSE	= ..\LICENSE

# One-click install (drivers + --enable-audit-log + --enable-toast) — see setup.bat's own
# header comment. Not a build output — copied straight from source, same as the toast icon.
TARGET_SETUP_BAT	= setup.bat

# Distribution-specific README (evaluation-build install/usage instructions, separate from
# README.md's developer-facing repo documentation above) — not a build output, copied straight
# from source. Kept as two fixed-language files rather than one auto-selected pair (contrast
# the Pro/Subscription editions' MSI-driven README.en-US.txt/README.ja-JP.txt, where the
# installer language picks one) since this CLI zip has no installer UI to make that choice for.
TARGET_DIST_README_JA	= dist-readme\README.ja-JP.txt

TARGET_DIST_README_EN	= dist-readme\README.en-US.txt

SIGNED_DIR	= Signed

# No intermediate "Drivers\" level: installer/install.cpp looks for
# <exeDir>\<PackageName>\<arch>\<PackageName>.inf directly (kbdaddid/mouaddid convention, plus
# an <arch> ("x64"/"arm64") level — see common.h's IsNativeArm64()), not
# <exeDir>\Drivers\<PackageName>\...
SIGNED_DRIVERS_DIR_KBD	= $(SIGNED_DIR)\oib_kbd\x64

SIGNED_DRIVERS_DIR_KBD_ARM64	= $(SIGNED_DIR)\oib_kbd\arm64

SIGNED_DRIVERS_DIR_MOU	= $(SIGNED_DIR)\oib_mou\x64

SIGNED_DRIVERS_DIR_MOU_ARM64	= $(SIGNED_DIR)\oib_mou\arm64

SIGNED_SYMBOLS_DIR	= $(SIGNED_DIR)\Symbol

DIST_DIR	= dist

DIST_ZIP	= $(DIST_DIR)\OpenInputBridge.zip

# tools		###############################################################

SIGNTOOL	= "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"

# rules		###############################################################

# Full local-EV-signing pipeline: everything in Signed\ comes from our own Release build.
all: sign stage

# .cat is signed alongside .sys/.exe: a catalog is itself an Authenticode-signable file, and
# an unsigned catalog is no better than no catalog at all for code-integrity purposes. .inf
# and .pdb are not signed (an INF is plain text with no Authenticode container, and a PDB is a
# debug-symbol file with no bearing on what actually loads/runs).
# sign: sign-bin sign-driver
sign: sign-all

sign-bin:
		@echo [sign-bin] signing installer (x64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN)
		@echo [sign-bin] signing installer (ARM64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN_ARM64)
		@echo [sign-bin] signing toast helper
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_TOAST_HELPER)
		@echo [sign-bin] done

sign-driver:
		@echo [sign-driver] signing keyboard driver package (x64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_KBD) $(TARGET_CAT_KBD)
		@echo [sign-driver] signing keyboard driver package (ARM64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_KBD_ARM64) $(TARGET_CAT_KBD_ARM64)
		@echo [sign-driver] signing mouse driver package (x64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_MOU) $(TARGET_CAT_MOU)
		@echo [sign-driver] signing mouse driver package (ARM64)
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_MOU_ARM64) $(TARGET_CAT_MOU_ARM64)
		@echo [sign-driver] done

sign-all:
		@echo [sign-all] signing all files.
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN) $(TARGET_BIN_ARM64) $(TARGET_TOAST_HELPER) $(TARGET_SYS_KBD) $(TARGET_CAT_KBD) $(TARGET_SYS_KBD_ARM64) $(TARGET_CAT_KBD_ARM64) $(TARGET_SYS_MOU) $(TARGET_CAT_MOU) $(TARGET_SYS_MOU_ARM64) $(TARGET_CAT_MOU_ARM64)
		@echo [sign-all] done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN_ARM64)
		$(SIGNTOOL) verify /v /pa $(TARGET_TOAST_HELPER)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_KBD)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_KBD)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_KBD_ARM64)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_KBD_ARM64)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_MOU)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_MOU)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_MOU_ARM64)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_MOU_ARM64)

# Copies the already-signed binaries (plus the unsigned .inf/.pdb) into a clean staging
# folder, laid out the way installer/install.cpp and an HLK submission both expect.
#
# Destinations end in "\." rather than a bare "\": a trailing backslash as the last character
# of an nmake command line is parsed as a line-continuation marker (merging the next command
# into this one), not as part of the path.
stage: stage-bin stage-driver stage-symbol

stage-bin:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		copy /y $(TARGET_BIN) $(SIGNED_DIR)\.
		copy /y $(TARGET_BIN_ARM64) $(SIGNED_DIR)\.
		copy /y $(TARGET_TOAST_HELPER) $(SIGNED_DIR)\.
		copy /y $(TARGET_TOAST_ICON) $(SIGNED_DIR)\.
		copy /y $(TARGET_README) $(SIGNED_DIR)\.
		copy /y $(TARGET_LICENSE) $(SIGNED_DIR)\.
		copy /y $(TARGET_SETUP_BAT) $(SIGNED_DIR)\.
		copy /y $(TARGET_DIST_README_JA) $(SIGNED_DIR)\.
		copy /y $(TARGET_DIST_README_EN) $(SIGNED_DIR)\.
		@echo [stage-bin] copied installer, toast helper, its icon, README, LICENSE, setup.bat, and the dist-readme files into $(SIGNED_DIR)

stage-driver:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_DRIVERS_DIR_KBD) mkdir $(SIGNED_DRIVERS_DIR_KBD)
		@if not exist $(SIGNED_DRIVERS_DIR_KBD_ARM64) mkdir $(SIGNED_DRIVERS_DIR_KBD_ARM64)
		@if not exist $(SIGNED_DRIVERS_DIR_MOU) mkdir $(SIGNED_DRIVERS_DIR_MOU)
		@if not exist $(SIGNED_DRIVERS_DIR_MOU_ARM64) mkdir $(SIGNED_DRIVERS_DIR_MOU_ARM64)
		copy /y $(TARGET_INF_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_CAT_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_SYS_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_INF_KBD_ARM64) $(SIGNED_DRIVERS_DIR_KBD_ARM64)\.
		copy /y $(TARGET_CAT_KBD_ARM64) $(SIGNED_DRIVERS_DIR_KBD_ARM64)\.
		copy /y $(TARGET_SYS_KBD_ARM64) $(SIGNED_DRIVERS_DIR_KBD_ARM64)\.
		copy /y $(TARGET_INF_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		copy /y $(TARGET_CAT_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		copy /y $(TARGET_SYS_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		copy /y $(TARGET_INF_MOU_ARM64) $(SIGNED_DRIVERS_DIR_MOU_ARM64)\.
		copy /y $(TARGET_CAT_MOU_ARM64) $(SIGNED_DRIVERS_DIR_MOU_ARM64)\.
		copy /y $(TARGET_SYS_MOU_ARM64) $(SIGNED_DRIVERS_DIR_MOU_ARM64)\.
		@echo [stage-driver] copied all four driver packages (keyboard/mouse x x64/ARM64) into $(SIGNED_DIR)

stage-symbol:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_SYMBOLS_DIR) mkdir $(SIGNED_SYMBOLS_DIR)
		copy /y $(TARGET_PDB_KBD) $(SIGNED_SYMBOLS_DIR)\.
		copy /y $(TARGET_PDB_KBD_ARM64) $(SIGNED_SYMBOLS_DIR)\oib_kbd_arm64.pdb
		copy /y $(TARGET_PDB_MOU) $(SIGNED_SYMBOLS_DIR)\.
		copy /y $(TARGET_PDB_MOU_ARM64) $(SIGNED_SYMBOLS_DIR)\oib_mou_arm64.pdb
		@echo [stage-symbol] copied all four pdbs (keyboard/mouse x x64/ARM64) into $(SIGNED_SYMBOLS_DIR)

# Post-WHQL pipeline — see the header comment above for why this only ever touches the
# installer .exe and Symbol\, never Signed\oib_kbd\/oib_mou\.
whql: sign-bin stage-bin stage-symbol
		@echo [whql] installer signed + staged, Symbol\ refreshed. Signed\oib_kbd\/oib_mou\ left as-is:
		@echo [whql] make sure the HLK/WHQL-returned .inf/.cat/.sys for both keyboard and mouse,
		@echo [whql] for BOTH x64 and ARM64 (two independent HLK submissions), are already sitting in
		@echo [whql] Signed\oib_kbd\x64\, Signed\oib_kbd\arm64\, Signed\oib_mou\x64\, and Signed\oib_mou\arm64\.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
# Deliberately does NOT depend on "all"/"stage"/"whql": it needs to work standalone from just
# whatever's currently sitting in Signed\, regardless of how it got there.
package:
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
