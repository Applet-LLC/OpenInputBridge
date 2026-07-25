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
# Usage (from an EWDK/VS "amd64" build environment, so nmake and signtool are on PATH):
#   nmake -f packaging\sign.mak            (sign only)
#   nmake -f packaging\sign.mak stage      (sign, then stage into Signed\)
#   nmake -f packaging\sign.mak package    (stage, then zip Signed\ into dist\)
#
# Prerequisite: both projects must already be built in Release|x64 (see README.md's ビルド方法).
# driver/OpenInputBridge.vcxproj's Inf2Cat step (re-enabled once OpenInputBridge.inx became a
# proper primitive-driver INF — see its own header comment) produces the driver package folder
# this reads from: x64\Release\OpenInputBridge\{OpenInputBridge.inf, openinputbridge.cat,
# OpenInputBridge.sys}.
#
# Signed\ is a deliberate hand-off point, not just a copy for convenience: once this driver
# goes through HLK/WHQL, the drivers\ (inf+cat+sys) and Symbol\ (pdb) folders HLK asks for as
# submission input are exactly what ends up here, and whatever comes back from that process can
# replace what sign.mak would otherwise produce, dropped into Signed\ the same way — `package`
# below doesn't care how the files it zips got signed, only that they're sitting in Signed\.
# Neither Signed\ nor dist\ are checked into git (build/signing output, not source — see
# .gitignore).
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

SIGNED_DRIVERS_DIR	= $(SIGNED_DIR)\drivers

SIGNED_SYMBOLS_DIR	= $(SIGNED_DIR)\Symbol

DIST_DIR	= dist

DIST_ZIP	= $(DIST_DIR)\OpenInputBridge.zip

# tools		###############################################################

SIGNTOOL	= "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"

# rules		###############################################################

# .cat is signed alongside .sys/.exe: a catalog is itself an Authenticode-signable file, and
# an unsigned catalog is no better than no catalog at all for code-integrity purposes. .inf
# and .pdb are not signed (an INF is plain text with no Authenticode container, and a PDB is a
# debug-symbol file with no bearing on what actually loads/runs).
all:
		@echo [sign] signing start
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN) $(TARGET_SYS) $(TARGET_CAT)
		@echo [sign] signing done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS)
		$(SIGNTOOL) verify /v /pa $(TARGET_CAT)

# Copies the already-signed binaries (plus the unsigned .inf/.pdb) into a clean staging
# folder, laid out the way installer/install.cpp and an HLK submission both expect. Deliberately
# does NOT depend on "all": re-running "all" re-signs (Authenticode just stacks another
# signature on top rather than erroring, but there's no reason to do that every time you
# re-stage) — run "all" once, then "stage"/"package" as often as needed.
#
# Destinations end in "\." rather than a bare "\": a trailing backslash as the last character
# of an nmake command line is parsed as a line-continuation marker (merging the next command
# into this one), not as part of the path.
stage:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		@if not exist $(SIGNED_DRIVERS_DIR) mkdir $(SIGNED_DRIVERS_DIR)
		@if not exist $(SIGNED_SYMBOLS_DIR) mkdir $(SIGNED_SYMBOLS_DIR)
		copy /y $(TARGET_BIN) $(SIGNED_DIR)\.
		copy /y $(TARGET_INF) $(SIGNED_DRIVERS_DIR)\.
		copy /y $(TARGET_CAT) $(SIGNED_DRIVERS_DIR)\.
		copy /y $(TARGET_SYS) $(SIGNED_DRIVERS_DIR)\.
		copy /y $(TARGET_PDB) $(SIGNED_SYMBOLS_DIR)\.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
package: stage
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
