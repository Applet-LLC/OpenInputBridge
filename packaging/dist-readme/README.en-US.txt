OpenInputBridge Installation Guide  1.00 2026-08-20 Applet LLC
==============================================================

This package installs "OpenInputBridge", a Windows kernel driver that
intercepts and re-injects keyboard/mouse input, compatible with the
Interception protocol.
This is an evaluation build: the device driver is either test-signed or
EV-signed.

Requirements
------------
- Windows 11 24H2 or later
- Administrator privileges for installation
- Test signing mode
  Your environment must be configured to allow test-signed device
  drivers to load. That means BitLocker and Secure Boot must be off,
  and you must run "bcdedit /set TESTSIGNING ON" and then restart.

Installation
------------
1. Run "setup.bat".
2. A system reboot is required to complete installation.

IMPORTANT: Where you extract this zip
This zip installs in place -- wherever you extract it becomes the install
location. Do not move or delete this folder after installing. Also, do not
extract it under a folder synced by OneDrive (including a "Desktop" folder
that is set to sync). We've confirmed on real hardware that this breaks the
audit-log/toast-notification feature. We recommend extracting to a plain
local folder that isn't synced, such as "C:\OpenInputBridge".

Files in this package
----------------------
- setup.bat
  Batch file that runs the setup.

- OpenInputBridgeSetup.exe
  The setup program itself.

- oib_kbd, oib_mou, Symbol
  The device driver and symbol files built from OpenInputBridge.
  These files may not be redistributed. The device driver is
  EV-signed, packaged this way for evaluation convenience.

- OibToastHelper.exe
- OibToastHelper.ico
  The binary and icon file used to show a toast notification when the
  device driver is accessed.

- LICENSE
  States that the source code is under the MIT License.

- README.md
  The README from https://github.com/Applet-LLC/OpenInputBridge.
  It has more detailed documentation -- please also see the
  disclaimer there. It's written in Japanese; please translate it
  into your own language if needed.

- README.en-US.txt
- README.ja-JP.txt
  This README.

Audit Logging / Toast Notifications
--------------------------------------
Installing via setup.bat enables both "Audit Logging" and "Toast
Notifications".

This records which process opened an Interception-compatible device
in the standard Windows Security event log, and shows a desktop toast
notification right when it happens.

Specifically: open "Event Viewer -> Windows Logs -> Security" and look
for entries with Event ID 4656 where ObjectName is
\Device\interceptionNN (NN is a number from 00 to 19). Check whether
the process name is the one you expect. If an unfamiliar process name
shows up there, it means that process is actually able to observe/
inject Interception-protocol-compatible keyboard/mouse input.

If you don't want notifications for specific software you already
trust, run the following from an elevated Command Prompt (the
Security event log record is kept regardless of whether a process is
allowlisted).

To add to the allowlist
    OpenInputBridgeSetup.exe --allow-process "C:\full\path\to\app.exe"

To remove from the allowlist
    OpenInputBridgeSetup.exe --disallow-process "C:\full\path\to\app.exe"

To show the current allowlist
    OpenInputBridgeSetup.exe --list-allowed-processes


Uninstallation
--------------
Run "OpenInputBridgeSetup.exe /uninstall" from an elevated prompt,
then restart your PC.

Do not use "pnputil -d oemXX.inf" to uninstall the device driver.
Doing so leaves the uninstall incomplete, and your keyboard/mouse will
become unusable after the next reboot.


Purchase
--------------
A WHQL-signed device driver is available if you purchase the
Subscription or Pro edition below. Please consider it.

https://applet.gumroad.com/l/hbxqex
OpenInputBridge-Subscription
Billed every 3 months.

https://applet.gumroad.com/l/xsggij
OpenInputBridge-Pro
One-time payment.


Source Code
-----------
https://github.com/Applet-LLC/OpenInputBridge

The source code is available under the MIT License.
The device driver bundled in this zip file is for evaluation only and
may not be redistributed.



Vendor / Developer
------------
Applet LLC
https://appletllc.com/ appletllc@gmail.com
