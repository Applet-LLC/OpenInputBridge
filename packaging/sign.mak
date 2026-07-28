# Copyright (c) 2026 OpenInputBridge Contributors
# SPDX-License-Identifier: MIT
# Licensed under the MIT License. See LICENSE file in the project root for full license text.
#
# EV code-signing + distribution packaging for Release binaries (M7). The signing part mirrors
# the Applet LLC "nodoka" project's own d.sign/d.sign.mak (private signing certificate, not
# part of this public repo).
#
# Signs directly (no signature-stripping pass): nothing here ships a pre-signed third-party
# binary, so there's no existing signature to remove before applying ours. The driver .sys is
# built with $(SignMode)=Off for Release (see driver/OpenInputBridge.vcxproj) specifically so
# it arrives here unsigned — no leftover WDK test signature to worry about, unlike Debug builds.
#
# Normally invoked through ..\OpenInputBridge.sln's "Packaging" project (a Makefile-type
# project — see Packaging.vcxproj), which runs this with the right target list per solution
# configuration (Release -> "all package", ReleaseWHQL -> "whql package"). Called directly:
#   nmake -f packaging\sign.mak            (sign, then stage into Signed\ — everything, EV-only)
#   nmake -f packaging\sign.mak sign       (sign only: installer .exe + driver .sys/.cat)
#   nmake -f packaging\sign.mak sign-bin   (sign only: installer .exe)
#   nmake -f packaging\sign.mak sign-driver (sign only: driver .sys/.cat)
#   nmake -f packaging\sign.mak stage      (stage only, no signing)
#   nmake -f packaging\sign.mak whql       (post-WHQL: sign+stage installer .exe and refresh
#                                            Symbol\, WITHOUT touching Signed\Drivers\)
#   nmake -f packaging\sign.mak package    (zip whatever's currently in Signed\ into dist\)
#
# Two distinct pipelines into Signed\, both ending in the same "package" step:
#
#   Pre-WHQL ("all"): this driver hasn't been through HLK/WHQL yet, so its .sys/.cat get our
#   own EV signature (attestation signing) same as the installer .exe. "all" = sign + stage
#   covers all of Signed\ from our own Release build output.
#
#   Post-WHQL ("whql"): once this driver has gone through HLK/WHQL, its .inf/.cat/.sys no
#   longer come from local signing — they come back from that submission process and get
#   dropped into Signed\Drivers\ BY HAND (this makefile never writes there in this mode). Only
#   the installer .exe (never part of the WHQL submission) still needs local EV signing, and
#   Signed\Symbol\ still wants our own freshly-built .pdb (WHQL doesn't hand one back — the
#   submitted driver's debug symbols are exactly what we built). "whql" therefore only ever
#   touches the installer .exe and Symbol\, and leaves Drivers\ exactly as you placed it.
#
# "package" is deliberately independent of both "all" and "whql": it only ever looks at
# what's currently sitting in Signed\, so the same single step works regardless of which
# pipeline (or manual copy) put it there.
#
# Prerequisite: driver and installer must already be built in Release|x64 (see
# ..\OpenInputBridge.sln, or README.md's ビルド方法 for standalone project builds).
# driver/OpenInputBridge.vcxproj's Inf2Cat step produces the driver package folder this reads
# from: ..\driver\x64\Release\OpenInputBridge\{OpenInputBridge.inf, openinputbridge.cat,
# OpenInputBridge.sys}.
#
# Neither Signed\ nor dist\ are checked into git (build/signing output, not source — see
# .gitignore). Signed\ is not cleaned by a solution "Clean" of the ReleaseWHQL configuration
# (see Packaging.vcxproj) since it can hold hand-placed WHQL submission output that took days
# to get back — only dist\ is safe to blow away and regenerate on every build.
#
# Distribution layout (matches what a primitive-driver installer needs — see
# installer/install.cpp's InfPath lookup):
#   OpenInputBridgeSetup.exe
#   drivers\OpenInputBridge.inf
#   drivers\OpenInputBridge.cat
#   drivers\OpenInputBridge.sys
#   Symbol\OpenInputBridge.pdb

DRIVER_PACKAGE_DIR	= ..\driver\x64\Release\OpenInputBridge

TARGET_SYS	= $(DRIVER_PACKAGE_DIR)\OpenInputBridge.sys

TARGET_INF	= $(DRIVER_PACKAGE_DIR)\OpenInputBridge.inf

TARGET_CAT	= $(DRIVER_PACKAGE_DIR)\openinputbridge.cat

TARGET_PDB	= ..\driver\x64\Release\OpenInputBridge.pdb

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

SIGNED_DIR	= Signed

SIGNED_DRIVERS_DIR	= $(SIGNED_DIR)\Drivers

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
		@echo [sign-driver] signing driver package
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_SYS) $(TARGET_CAT)
		@echo [sign-driver] done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT)

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
		@if not exist $(SIGNED_DRIVERS_DIR) mkdir $(SIGNED_DRIVERS_DIR)
		copy /y $(TARGET_INF) $(SIGNED_DRIVERS_DIR)\.
		copy /y $(TARGET_CAT) $(SIGNED_DRIVERS_DIR)\.
		copy /y $(TARGET_SYS) $(SIGNED_DRIVERS_DIR)\.
		@echo [stage-driver] copied driver package into $(SIGNED_DRIVERS_DIR)

stage-symbol:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_SYMBOLS_DIR) mkdir $(SIGNED_SYMBOLS_DIR)
		copy /y $(TARGET_PDB) $(SIGNED_SYMBOLS_DIR)\.
		@echo [stage-symbol] copied pdb into $(SIGNED_SYMBOLS_DIR)

# Post-WHQL pipeline — see the header comment above for why this only ever touches the
# installer .exe and Symbol\, never Signed\Drivers\.
whql: sign-bin stage-bin stage-symbol
		@echo [whql] installer signed + staged, Symbol\ refreshed. Signed\Drivers\ left as-is:
		@echo [whql] make sure the HLK/WHQL-returned .inf/.cat/.sys are already sitting there.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
# Deliberately does NOT depend on "all"/"stage"/"whql": it needs to work standalone from just
# whatever's currently sitting in Signed\, regardless of how it got there.
package:
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
