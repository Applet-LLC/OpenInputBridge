# Copyright (c) 2026 OpenInputBridge Contributors
# SPDX-License-Identifier: MIT
# Licensed under the MIT License. See LICENSE file in the project root for full license text.
#
# EV code-signing for Release binaries (M7). Structure mirrors the Applet LLC "nodoka" project's
# own d.sign/d.sign.mak (private signing certificate, not part of this public repo) — see
# docs/CLEAN_ROOM.md if a provenance note is ever needed here.
#
# Signs directly (no signature-stripping pass): unlike some other Applet LLC projects, nothing
# here ships a pre-signed third-party binary, so there's no existing signature to remove before
# applying ours. The driver .sys picks up a WDK-generated *test* signature during a normal
# Release build (see driver/OpenInputBridge.vcxproj's DriverSign settings) — Authenticode
# supports multiple signers, so this just adds the real one alongside it; the test signature
# has no bearing on a properly EV-signed binary.
#
# Usage (from an EWDK/VS "amd64" build environment, so nmake and signtool are on PATH):
#   nmake -f packaging\sign.mak
#
# Prerequisite: both projects must already be built in Release|x64 (see README.md's ビルド方法).
# This does NOT run Inf2Cat/catalog generation — see OpenInputBridge.vcxproj's EnableInf2cat
# note and the M6/M7 TODO on driver/OpenInputBridge.inx for why that's still pending, separate
# from Authenticode-signing the .sys binary itself here.

TARGET_SYS	= ..\driver\x64\Release\OpenInputBridge.sys

TARGET_BIN	= ..\installer\x64\Release\OpenInputBridgeSetup.exe

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
