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
#   dropped into Signed\oib_kbd\ and Signed\oib_mou\ BY HAND (this makefile never writes there
#   in this mode). Only the installer .exe (never part of the WHQL submission) still needs
#   local EV signing. WHQL doesn't hand symbols back, so Signed\Symbol\ still needs our own
#   .pdb — but "our own" must mean the exact build that was actually submitted, not whatever
#   happens to be sitting in the local Release output right now: if the driver gets rebuilt
#   (even a no-op rebuild) between submission and running this target, a .pdb pulled fresh from
#   the build tree would silently no longer match the signed .sys HLK returned. To avoid that,
#   drop the submission-time .pdb into Signed\oib_kbd\/oib_mou\ BY HAND alongside the returned
#   .inf/.cat/.sys (see TARGET_PDB_KBD/MOU below) — it's used in preference to the local build
#   output when present. "whql" therefore only ever touches the installer .exe and Symbol\, and
#   leaves Signed\oib_kbd\/oib_mou\ exactly as you placed them.
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
# Prerequisite: both drivers and the installer must already be built in Release|x64 (see
# ..\OpenInputBridge.sln, or README.md's ビルド方法 for standalone project builds).
# driver/keyboard/oib_kbd.vcxproj's and driver/mouse/oib_mou.vcxproj's Inf2Cat steps each
# produce a driver package folder this reads from:
#   ..\driver\keyboard\x64\Release\oib_kbd\{oib_kbd.inf, oib_kbd.cat, oib_kbd.sys}
#   ..\driver\mouse\x64\Release\oib_mou\{oib_mou.inf, oib_mou.cat, oib_mou.sys}
#
# Neither Signed\ nor dist\ are checked into git (build/signing output, not source — see
# .gitignore). Signed\ is not cleaned by a solution "Clean" of the ReleaseWHQL configuration
# (see Packaging.vcxproj) since it can hold hand-placed WHQL submission output that took days
# to get back — only dist\ is safe to blow away and regenerate on every build.
#
# Distribution layout (matches what installer/install.cpp's InfPath lookup expects —
# <exeDir>\<PackageName>\<PackageName>.inf per driver):
#   setup.bat
#   OpenInputBridgeSetup.exe
#   OibToastHelper.exe
#   OibToastHelper.ico
#   README.md
#   LICENSE
#   oib_kbd\oib_kbd.inf
#   oib_kbd\oib_kbd.cat
#   oib_kbd\oib_kbd.sys
#   oib_mou\oib_mou.inf
#   oib_mou\oib_mou.cat
#   oib_mou\oib_mou.sys
#   Symbol\oib_kbd.pdb
#   Symbol\oib_mou.pdb

DRIVER_PACKAGE_DIR_KBD	= ..\driver\keyboard\x64\Release\oib_kbd

DRIVER_PACKAGE_DIR_MOU	= ..\driver\mouse\x64\Release\oib_mou

TARGET_SYS_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.sys

TARGET_INF_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.inf

TARGET_CAT_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\oib_kbd.cat

# Prefers a hand-placed, submission-matching .pdb (Signed\oib_kbd\oib_kbd.pdb, dropped in next
# to the WHQL-returned .inf/.cat/.sys) over the local Release build output, since the latter can
# silently drift out of sync with what was actually submitted (see header comment above).
!IF EXIST(Signed\oib_kbd\oib_kbd.pdb)
TARGET_PDB_KBD	= Signed\oib_kbd\oib_kbd.pdb
!ELSE
TARGET_PDB_KBD	= ..\driver\keyboard\x64\Release\oib_kbd.pdb
!ENDIF

TARGET_SYS_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.sys

TARGET_INF_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.inf

TARGET_CAT_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\oib_mou.cat

!IF EXIST(Signed\oib_mou\oib_mou.pdb)
TARGET_PDB_MOU	= Signed\oib_mou\oib_mou.pdb
!ELSE
TARGET_PDB_MOU	= ..\driver\mouse\x64\Release\oib_mou.pdb
!ENDIF

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

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

SIGNED_DIR	= Signed

# No intermediate "Drivers\" level: installer/install.cpp looks for
# <exeDir>\<PackageName>\<PackageName>.inf directly (kbdaddid/mouaddid convention — see
# common.h), not <exeDir>\Drivers\<PackageName>\...
SIGNED_DRIVERS_DIR_KBD	= $(SIGNED_DIR)\oib_kbd

SIGNED_DRIVERS_DIR_MOU	= $(SIGNED_DIR)\oib_mou

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
		@echo [sign-bin] signing installer
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN)
		@echo [sign-bin] signing toast helper
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_TOAST_HELPER)
		@echo [sign-bin] done

sign-driver:
		@echo [sign-driver] signing keyboard driver package
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_KBD) $(TARGET_CAT_KBD)
		@echo [sign-driver] signing mouse driver package
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_MOU) $(TARGET_CAT_MOU)
		@echo [sign-driver] done

sign-all:
		@echo [sign-all] signing all files.
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN) $(TARGET_TOAST_HELPER) $(TARGET_SYS_KBD) $(TARGET_CAT_KBD) $(TARGET_SYS_MOU) $(TARGET_CAT_MOU)
		@echo [sign-all] done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
		$(SIGNTOOL) verify /v /pa $(TARGET_TOAST_HELPER)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_KBD)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_KBD)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS_MOU)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT_MOU)

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
		copy /y $(TARGET_TOAST_HELPER) $(SIGNED_DIR)\.
		copy /y $(TARGET_TOAST_ICON) $(SIGNED_DIR)\.
		copy /y $(TARGET_README) $(SIGNED_DIR)\.
		copy /y $(TARGET_LICENSE) $(SIGNED_DIR)\.
		copy /y $(TARGET_SETUP_BAT) $(SIGNED_DIR)\.
		@echo [stage-bin] copied installer, toast helper, its icon, README, LICENSE, and setup.bat into $(SIGNED_DIR)

stage-driver:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_DRIVERS_DIR_KBD) mkdir $(SIGNED_DRIVERS_DIR_KBD)
		@if not exist $(SIGNED_DRIVERS_DIR_MOU) mkdir $(SIGNED_DRIVERS_DIR_MOU)
		copy /y $(TARGET_INF_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_CAT_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_SYS_KBD) $(SIGNED_DRIVERS_DIR_KBD)\.
		copy /y $(TARGET_INF_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		copy /y $(TARGET_CAT_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		copy /y $(TARGET_SYS_MOU) $(SIGNED_DRIVERS_DIR_MOU)\.
		@echo [stage-driver] copied both driver packages into $(SIGNED_DIR)

stage-symbol:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_SYMBOLS_DIR) mkdir $(SIGNED_SYMBOLS_DIR)
		copy /y $(TARGET_PDB_KBD) $(SIGNED_SYMBOLS_DIR)\.
		copy /y $(TARGET_PDB_MOU) $(SIGNED_SYMBOLS_DIR)\.
		@echo [stage-symbol] copied both pdbs into $(SIGNED_SYMBOLS_DIR)

# Post-WHQL pipeline — see the header comment above for why this only ever touches the
# installer .exe and Symbol\, never Signed\oib_kbd\/oib_mou\.
whql: sign-bin stage-bin stage-symbol
		@echo [whql] installer signed + staged, Symbol\ refreshed. Signed\oib_kbd\/oib_mou\ left as-is:
		@echo [whql] make sure the HLK/WHQL-returned .inf/.cat/.sys for both keyboard and mouse
		@echo [whql] are already sitting in Signed\oib_kbd\ and Signed\oib_mou\.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
# Deliberately does NOT depend on "all"/"stage"/"whql": it needs to work standalone from just
# whatever's currently sitting in Signed\, regardless of how it got there.
package:
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
