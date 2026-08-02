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
# are built with $(SignMode)=Off for Release (see driver/keyboard/keyboard.vcxproj and
# driver/mouse/mouse.vcxproj) specifically so they arrive here unsigned — no leftover WDK test
# signature to worry about, unlike Debug builds.
#
# Normally invoked through ..\OpenInputBridge.sln's "Packaging" project (a Makefile-type
# project — see Packaging.vcxproj), which runs this with the right target list per solution
# configuration (Release -> "all package", ReleaseWHQL -> "whql package"). Called directly:
#   nmake -f packaging\sign.mak            (sign, then stage into Signed\ — everything, EV-only)
#   nmake -f packaging\sign.mak sign       (sign only: installer .exe + both driver .sys/.cat)
#   nmake -f packaging\sign.mak sign-bin   (sign only: installer .exe)
#   nmake -f packaging\sign.mak sign-driver (sign only: both drivers' .sys/.cat)
#   nmake -f packaging\sign.mak stage      (stage only, no signing)
#   nmake -f packaging\sign.mak whql       (post-WHQL: sign+stage installer .exe and refresh
#                                            Symbol\, WITHOUT touching Signed\keyboard\/mouse\)
#   nmake -f packaging\sign.mak package    (zip whatever's currently in Signed\ into dist\)
#
# Since docs/DECISIONS.md's 2026-07-30 entry, this ships TWO independent driver packages
# (keyboard.sys/Class=Keyboard and mouse.sys/Class=Mouse — see driver/keyboard/, driver/mouse/)
# instead of the original single OpenInputBridge.sys/Class=System binary. Every target below
# that used to touch one driver package now touches both.
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
#   dropped into Signed\keyboard\ and Signed\mouse\ BY HAND (this makefile never writes there
#   in this mode). Only the installer .exe (never part of the WHQL submission) still needs
#   local EV signing, and Signed\Symbol\ still wants our own freshly-built .pdb files (WHQL
#   doesn't hand them back — the submitted drivers' debug symbols are exactly what we built).
#   "whql" therefore only ever touches the installer .exe and Symbol\, and leaves
#   Signed\keyboard\/mouse\ exactly as you placed them.
#
# "package" is deliberately independent of both "all" and "whql": it only ever looks at
# what's currently sitting in Signed\, so the same single step works regardless of which
# pipeline (or manual copy) put it there.
#
# Prerequisite: both drivers and the installer must already be built in Release|x64 (see
# ..\OpenInputBridge.sln, or README.md's ビルド方法 for standalone project builds).
# driver/keyboard/keyboard.vcxproj's and driver/mouse/mouse.vcxproj's Inf2Cat steps each
# produce a driver package folder this reads from:
#   ..\driver\keyboard\x64\Release\keyboard\{keyboard.inf, keyboard.cat, keyboard.sys}
#   ..\driver\mouse\x64\Release\mouse\{mouse.inf, mouse.cat, mouse.sys}
#
# Neither Signed\ nor dist\ are checked into git (build/signing output, not source — see
# .gitignore). Signed\ is not cleaned by a solution "Clean" of the ReleaseWHQL configuration
# (see Packaging.vcxproj) since it can hold hand-placed WHQL submission output that took days
# to get back — only dist\ is safe to blow away and regenerate on every build.
#
# Distribution layout (matches what installer/install.cpp's InfPath lookup expects —
# <exeDir>\<PackageName>\<PackageName>.inf per driver):
#   OpenInputBridgeSetup.exe
#   keyboard\keyboard.inf
#   keyboard\keyboard.cat
#   keyboard\keyboard.sys
#   mouse\mouse.inf
#   mouse\mouse.cat
#   mouse\mouse.sys
#   Symbol\keyboard.pdb
#   Symbol\mouse.pdb

DRIVER_PACKAGE_DIR_KBD	= ..\driver\keyboard\x64\Release\keyboard

DRIVER_PACKAGE_DIR_MOU	= ..\driver\mouse\x64\Release\mouse

TARGET_SYS_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\keyboard.sys

TARGET_INF_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\keyboard.inf

TARGET_CAT_KBD	= $(DRIVER_PACKAGE_DIR_KBD)\keyboard.cat

TARGET_PDB_KBD	= ..\driver\keyboard\x64\Release\keyboard.pdb

TARGET_SYS_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\mouse.sys

TARGET_INF_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\mouse.inf

TARGET_CAT_MOU	= $(DRIVER_PACKAGE_DIR_MOU)\mouse.cat

TARGET_PDB_MOU	= ..\driver\mouse\x64\Release\mouse.pdb

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

SIGNED_DIR	= Signed

# No intermediate "Drivers\" level: installer/install.cpp looks for
# <exeDir>\<PackageName>\<PackageName>.inf directly (kbdaddid/mouaddid convention — see
# common.h), not <exeDir>\Drivers\<PackageName>\...
SIGNED_DRIVERS_DIR_KBD	= $(SIGNED_DIR)\keyboard

SIGNED_DRIVERS_DIR_MOU	= $(SIGNED_DIR)\mouse

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
sign: sign-bin sign-driver

sign-bin:
		@echo [sign-bin] signing installer
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN)
		@echo [sign-bin] done

sign-driver:
		@echo [sign-driver] signing keyboard driver package
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_KBD) $(TARGET_CAT_KBD)
		@echo [sign-driver] signing mouse driver package
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS_MOU) $(TARGET_CAT_MOU)
		@echo [sign-driver] done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
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
		@echo [stage-bin] copied installer into $(SIGNED_DIR)

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
# installer .exe and Symbol\, never Signed\keyboard\/mouse\.
whql: sign-bin stage-bin stage-symbol
		@echo [whql] installer signed + staged, Symbol\ refreshed. Signed\keyboard\/mouse\ left as-is:
		@echo [whql] make sure the HLK/WHQL-returned .inf/.cat/.sys for both keyboard and mouse
		@echo [whql] are already sitting in Signed\keyboard\ and Signed\mouse\.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
# Deliberately does NOT depend on "all"/"stage"/"whql": it needs to work standalone from just
# whatever's currently sitting in Signed\, regardless of how it got there.
package:
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
