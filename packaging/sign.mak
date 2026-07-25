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
# This does NOT run Inf2Cat/catalog generation — see OpenInputBridge.vcxproj's EnableInf2cat
# note and the M6/M7 TODO on driver/OpenInputBridge.inx for why that's still pending, separate
# from Authenticode-signing the .sys binary itself here.
#
# Signed\ is a deliberate hand-off point, not just a copy for convenience: once this driver
# goes through Microsoft attestation/WHQL signing, the files Partner Center hands back replace
# what sign.mak would otherwise produce, dropped into Signed\ the same way — `package` below
# doesn't care how the files it zips got signed, only that they're sitting in Signed\. Neither
# Signed\ nor dist\ are checked into git (build/signing output, not source — see .gitignore).

TARGET_SYS	= ..\driver\x64\Release\OpenInputBridge.sys

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

SIGNED_DIR	= Signed

DIST_DIR	= dist

DIST_ZIP	= $(DIST_DIR)\OpenInputBridge.zip

# tools		###############################################################

SIGNTOOL	= "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"

# rules		###############################################################

all:
		@echo [sign] signing start
		-$(SIGNTOOL) sign /v /a /n "Applet LLC" /tr http://timestamp.globalsign.com/tsa/r45standard /td sha256 /fd sha256 /ph $(TARGET_BIN) $(TARGET_SYS)
		@echo [sign] signing done

verify:
		$(SIGNTOOL) verify /v /pa $(TARGET_BIN)
		$(SIGNTOOL) verify /v /pa $(TARGET_SYS)

# Copies the already-signed binaries into a clean staging folder. Deliberately does NOT
# depend on "all": re-running "all" re-signs (Authenticode just stacks another signature on
# top rather than erroring, but there's no reason to do that every time you re-stage) — run
# "all" once, then "stage"/"package" as often as needed. install.cpp expects
# OpenInputBridge.sys to sit alongside OpenInputBridgeSetup.exe at install time (see
# installer/install.cpp's GetModuleDirectory-based lookup), so Signed\ is exactly what a
# distributed copy of the installer needs to ship with.
#
# Destinations end in "\." rather than a bare "\": a trailing backslash as the last character
# of an nmake command line is parsed as a line-continuation marker (merging the next command
# into this one), not as part of the path.
stage:
		@if not exist $(SIGNED_DIR) mkdir $(SIGNED_DIR)
		copy /y $(TARGET_SYS) $(SIGNED_DIR)\.
		copy /y $(TARGET_BIN) $(SIGNED_DIR)\.

# Zips Signed\ as-is into dist\OpenInputBridge.zip. Uses PowerShell's Compress-Archive rather
# than a separate zip tool dependency — available on every supported Windows version.
package: stage
		@if not exist $(DIST_DIR) mkdir $(DIST_DIR)
		powershell -NoProfile -Command "Compress-Archive -Path '$(SIGNED_DIR)\*' -DestinationPath '$(DIST_ZIP)' -Force"
		@echo [package] created $(DIST_ZIP)
