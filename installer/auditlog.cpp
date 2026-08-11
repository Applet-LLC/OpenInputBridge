// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See auditlog.h.

#include "auditlog.h"
#include "common.h"

#include <aclapi.h>
#include <sddl.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace OpenInputBridge {

namespace {

std::wstring BuildDevicePath(ULONG index)
{
    wchar_t path[32];
    swprintf_s(path, L"\\\\.\\interception%02lu", index);
    return path;
}

// Enables (does not merely hold, but actually turns on within this process's token) a
// privilege that's disabled by default even for administrators — SeSecurityPrivilege is
// required for SACL_SECURITY_INFORMATION writes via SetNamedSecurityInfoW below.
bool EnablePrivilege(const wchar_t* privilegeName)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    DWORD lastError = GetLastError();
    CloseHandle(token);

    // AdjustTokenPrivileges can return TRUE while still not actually granting the privilege
    // (if the token never held it at all) — ERROR_NOT_ALL_ASSIGNED distinguishes that case.
    return adjusted && lastError != ERROR_NOT_ALL_ASSIGNED;
}

// Builds a SACL auditing both successful and failed GENERIC_READ|GENERIC_WRITE access by
// Everyone — this mirrors the DACL's own Everyone GRGW grant (driver/common/driver.c's
// OibControlDeviceSddl), so the audit log records exactly the access the DACL allows, no more
// and no less. aclBuffer owns the memory backing *outAcl; keep it alive as long as *outAcl is
// used.
bool BuildAuditSacl(std::vector<BYTE>& aclBuffer, PACL& outAcl)
{
    SID_IDENTIFIER_AUTHORITY worldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyoneSid = nullptr;

    if (!AllocateAndInitializeSid(
            &worldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyoneSid
            )) {
        return false;
    }

    DWORD aclSize = sizeof(ACL) +
        (sizeof(SYSTEM_AUDIT_ACE) - sizeof(DWORD)) + GetLengthSid(everyoneSid) +
        64; // slack for alignment padding.

    aclBuffer.assign(aclSize, 0);
    PACL acl = reinterpret_cast<PACL>(aclBuffer.data());

    // GENERIC_READ/GENERIC_WRITE must be mapped to their object-type-specific bits before going
    // into a stored ACE — an ACE holding the raw generic-right bits (0x80000000/0x40000000) is
    // not guaranteed to compare correctly against the specific-rights access mask an actual open
    // gets checked against. This mirrors a real-machine finding during this change: the DACL
    // (driver/common/driver.c's OibControlDeviceSddl) uses the same unmapped "GRGW" SDDL tokens
    // and access control still worked (Everyone can open the device), but with an unmapped SACL
    // here, only this installer's own SetNamedSecurityInfoW calls (which happen to request a
    // broader access set) were ever actually audited — the reference Interception client
    // library's own open call (third_party/interception/library/interception.c:
    // `CreateFileA(device_name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL)`, which is what
    // identify2.exe and any other Interception-protocol client ultimately uses) never triggered
    // an event at all. FILE_GENERIC_READ/FILE_GENERIC_WRITE is the correct mapping for a device
    // opened by name via CreateFile (SE_FILE_OBJECT), matching what ApplySaclToDevice/
    // ClearSaclOnDevice already use as the object type below.
    GENERIC_MAPPING fileGenericMapping = {
        FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
    };
    ACCESS_MASK auditedAccess = GENERIC_READ | GENERIC_WRITE;
    MapGenericMask(&auditedAccess, &fileGenericMapping);

    bool ok = InitializeAcl(acl, aclSize, ACL_REVISION) &&
        AddAuditAccessAceEx(
            acl, ACL_REVISION, 0 /* no inheritance: not a container */,
            auditedAccess, everyoneSid, TRUE /* audit success */, TRUE /* audit failure */
            );

    FreeSid(everyoneSid);

    if (!ok) {
        return false;
    }

    outAcl = acl;
    return true;
}

// Distinguishes "actually wrote/cleared the SACL" from "the device doesn't exist right now"
// (ERROR_FILE_NOT_FOUND — expected if only one of the keyboard/mouse drivers is installed, see
// docs/DECISIONS.md's 2026-08-02 entry, or if neither has created its control devices yet, e.g.
// right after a fresh install before the first reboot) and from a genuine hard failure. Collapsing
// NotPresent into "success" (as this used to do via a plain bool) made ForEachControlDevice's
// count indistinguishable between "applied to N devices" and "N devices simply don't exist" —
// see docs/DECISIONS.md's 2026-08-11 audit-log/toast review entry for the two call sites
// (RunApplyAuditSacl, RunEnableAuditLog) whose own "0 means something's wrong" logic that bug
// silently defeated.
enum class SaclOpResult { Applied, NotPresent, Failed };

// Clears the SACL to empty (used, empty ACL — not nullptr/"no SACL", which SetNamedSecurityInfo
// treats as "leave whatever is there alone" rather than "remove it").
SaclOpResult ClearSaclOnDevice(const std::wstring& devicePath)
{
    BYTE emptyAclBuffer[sizeof(ACL)];
    PACL emptyAcl = reinterpret_cast<PACL>(emptyAclBuffer);
    InitializeAcl(emptyAcl, sizeof(emptyAclBuffer), ACL_REVISION);

    DWORD result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(devicePath.c_str()), SE_FILE_OBJECT, SACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, emptyAcl
        );

    if (result == ERROR_FILE_NOT_FOUND) {
        return SaclOpResult::NotPresent;
    }
    if (result != ERROR_SUCCESS) {
        wprintf(L"[WARN] Failed to clear audit SACL on %s: %lu\n", devicePath.c_str(), result);
        return SaclOpResult::Failed;
    }
    return SaclOpResult::Applied;
}

SaclOpResult ApplySaclToDevice(const std::wstring& devicePath, PACL sacl)
{
    DWORD result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(devicePath.c_str()), SE_FILE_OBJECT, SACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, sacl
        );

    if (result == ERROR_FILE_NOT_FOUND) {
        // Expected if only one of the keyboard/mouse drivers is installed (not the normal
        // configuration — see docs/DECISIONS.md's 2026-08-02 entry — but not a hard error
        // here either).
        return SaclOpResult::NotPresent;
    }
    if (result != ERROR_SUCCESS) {
        wprintf(L"[WARN] Failed to set audit SACL on %s: %lu\n", devicePath.c_str(), result);
        return SaclOpResult::Failed;
    }
    return SaclOpResult::Applied;
}

// Diagnostic only (--dump-audit-sacl): reads back whatever SACL is actually live on a device
// right now via GetNamedSecurityInfoW, independent of what this installer thinks it set —
// added because real-machine testing kept finding that a genuine Interception-protocol client's
// CreateFileA(GENERIC_READ) open of a device never generates 4656 even after confirming
// (via auditpol) that both the "Kernel Object" and "File System" subcategories are on, which
// leaves "is the ACE we intended to write actually the one sitting on the live object" as the
// one remaining unverified assumption. See docs/DECISIONS.md's 2026-08-10 entry.
void DumpAuditSaclForDevice(const std::wstring& devicePath)
{
    PACL sacl = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;

    DWORD result = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(devicePath.c_str()), SE_FILE_OBJECT, SACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, &sacl, &securityDescriptor
        );
    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] GetNamedSecurityInfoW(%s) failed: %lu\n", devicePath.c_str(), result);
        return;
    }

    if (sacl == nullptr) {
        wprintf(L"%s: no SACL present.\n", devicePath.c_str());
        LocalFree(securityDescriptor);
        return;
    }

    ACL_SIZE_INFORMATION sizeInfo{};
    if (!GetAclInformation(sacl, &sizeInfo, sizeof(sizeInfo), AclSizeInformation)) {
        wprintf(L"[ERROR] GetAclInformation failed: %lu\n", GetLastError());
        LocalFree(securityDescriptor);
        return;
    }

    wprintf(L"%s: SACL has %lu ACE(s).\n", devicePath.c_str(), sizeInfo.AceCount);

    for (DWORD i = 0; i < sizeInfo.AceCount; ++i) {
        LPVOID acePtr = nullptr;
        if (!GetAce(sacl, i, &acePtr)) {
            continue;
        }

        PACE_HEADER header = reinterpret_cast<PACE_HEADER>(acePtr);
        wchar_t* sidString = nullptr;
        ACCESS_MASK mask = 0;
        PSID sid = nullptr;

        if (header->AceType == SYSTEM_AUDIT_ACE_TYPE) {
            PSYSTEM_AUDIT_ACE auditAce = reinterpret_cast<PSYSTEM_AUDIT_ACE>(acePtr);
            mask = auditAce->Mask;
            sid = reinterpret_cast<PSID>(&auditAce->SidStart);
        }

        if (sid != nullptr && ConvertSidToStringSidW(sid, &sidString)) {
            wprintf(
                L"  [%lu] type=%u flags=0x%02x mask=0x%08lx sid=%s "
                L"(success=%d failure=%d)\n",
                i, header->AceType, header->AceFlags, mask, sidString,
                (header->AceFlags & SUCCESSFUL_ACCESS_ACE_FLAG) != 0,
                (header->AceFlags & FAILED_ACCESS_ACE_FLAG) != 0
                );
            LocalFree(sidString);
        } else {
            wprintf(L"  [%lu] type=%u flags=0x%02x (unrecognized ACE type)\n", i, header->AceType, header->AceFlags);
        }
    }

    LocalFree(securityDescriptor);
}

// Applies (apply=true) or clears (apply=false) the audit SACL on every \\.\interceptionNN in
// 0..TotalDeviceSlotCount-1. Devices that don't currently exist (ERROR_FILE_NOT_FOUND) are
// silently skipped rather than treated as failures, and are NOT counted in the returned total —
// see SaclOpResult's comment for why that distinction matters to this function's callers.
// Returns the number of devices the SACL was actually applied to/cleared from.
int ForEachControlDevice(bool apply)
{
    std::vector<BYTE> aclBuffer;
    PACL sacl = nullptr;

    if (apply && !BuildAuditSacl(aclBuffer, sacl)) {
        wprintf(L"[ERROR] Failed to build the audit SACL: %lu\n", GetLastError());
        return 0;
    }

    int appliedCount = 0;
    for (ULONG index = 0; index < TotalDeviceSlotCount; ++index) {
        std::wstring devicePath = BuildDevicePath(index);
        SaclOpResult result = apply ? ApplySaclToDevice(devicePath, sacl) : ClearSaclOnDevice(devicePath);
        if (result == SaclOpResult::Applied) {
            ++appliedCount;
        }
    }
    return appliedCount;
}

// auditpol.exe's /subcategory parameter matches against the *localized display name* of the
// subcategory, not a fixed English string — "Kernel Object" only works on an English-language
// Windows install. On a Japanese one (confirmed empirically: real-machine testing during this
// change reproduced auditpol exiting with ERROR_INVALID_PARAMETER (87) for "Kernel Object",
// while the same command using this GUID form parsed correctly), it's rejected outright as an
// unrecognized parameter. The subcategory GUID form (auditpol has supported "/subcategory:{GUID}"
// since Windows Vista) is locale-independent and avoids this entirely. This is the well-known,
// stable GUID for the "Kernel Object" subcategory (Object Access category) — see e.g.
// `auditpol /get /subcategory:{0CCE921F-69AE-11D9-BED3-505054503030}` on any locale.
inline constexpr wchar_t KernelObjectSubcategoryGuid[] = L"{0CCE921F-69AE-11D9-BED3-505054503030}";

// BuildAuditSacl passes SE_FILE_OBJECT to SetNamedSecurityInfoW/AddAuditAccessAceEx, and every
// audit event the SACL generates is logged with ObjectType="File" — that's true for every
// SE_FILE_OBJECT-typed handle regardless of whether it's a real NTFS file or (as here) a raw
// device object opened by name, since both go through the same FILE_OBJECT-based I/O path.
// Windows governs SE_FILE_OBJECT auditing through the "File System" subcategory, not "Kernel
// Object" (that one covers actual NT kernel objects like mutexes/semaphores, opened via
// OpenMutex/OpenSemaphore rather than CreateFile) — real-machine testing during this change
// found that with only "Kernel Object" enabled, this installer's own SetNamedSecurityInfoW
// calls were reliably audited (apparently through some other, SACL-modification-specific audit
// path) while a genuine Interception-protocol client's plain CreateFileA(GENERIC_READ) open —
// the actual case this feature exists to observe — never generated any event at all. Enabling
// "File System" alongside "Kernel Object" (rather than replacing it, since self-noise auditing
// via Kernel Object is confirmed working and harmless to leave on) is the fix. See
// docs/DECISIONS.md's 2026-08-10 entry.
//
// {0CCE9215-...} (a value this constant briefly held) is NOT File System — real-machine
// verification via `auditpol /get /subcategory:{0CCE9215-...}` showed it actually resolves to
// "Logon" (Logon/Logoff category), an off-by-several mistake in the subcategory GUID table.
// {0CCE921D-...} is the correct, auditpol-confirmed GUID for "File System" (Object Access
// category).
inline constexpr wchar_t FileSystemSubcategoryGuid[] = L"{0CCE921D-69AE-11D9-BED3-505054503030}";

// Even with both of the above enabled and the SACL confirmed correct (read back via
// --dump-audit-sacl), a genuine Interception-protocol client's plain CreateFileA(GENERIC_READ)
// open of a control device still produced zero events — isolated via real-machine testing to
// something specific to this driver's WdfControlDeviceInitAllocate-created control devices
// (not a regular process/elevation/library difference: a direct CreateFile probe run from an
// elevated PowerShell process reproduced the same silence a plain NTFS file with an equivalent
// SACL did not). Additionally enabling "Handle Manipulation" made the missing 4656 ("A handle
// to an object was requested") appear immediately for the same probe, with AccessMask 0x120089
// (FILE_GENERIC_READ) exactly matching the SACL. Apparently, for this kind of control-device
// object, 4656 generation at CreateFile time depends on this subcategory in a way it does not
// for a regular NTFS file (where "File System" alone was sufficient in the same testing) — the
// exact WDF/object-manager reason isn't confirmed, but the auditpol-plus-probe result is. See
// docs/DECISIONS.md's 2026-08-10 entry.
inline constexpr wchar_t HandleManipulationSubcategoryGuid[] = L"{0CCE9223-69AE-11D9-BED3-505054503030}";

// Windows carries two parallel audit policy systems: the legacy 9-category "basic" audit
// policy (Local/Group Policy's plain "Audit object access" etc.) and the fine-grained,
// auditpol.exe-driven per-subcategory policy this feature relies on. Unless the OS is told to
// let the subcategory settings win, the legacy category-level policy can take precedence (or
// interact with it unpredictably) — this is a documented Microsoft caveat, not specific to this
// feature, and matches a real-machine repro during this change: after enabling only the
// "Kernel Object" subcategory, the Security log started recording unrelated "Other Object
// Access Events" activity (Credential Manager reads, event 5379) while \\.\interceptionNN itself
// produced no events at all — consistent with the legacy category-level policy being in effect
// instead of (or in addition to) the subcategory-level one actually requested. Setting this
// value is the standard, Microsoft-documented fix (equivalent to enabling Local Security
// Policy's "Audit: Force audit policy subcategory settings (Windows Vista or later) to override
// audit policy category settings"). See docs/DECISIONS.md's 2026-08-10 entry.
//
// Deliberately never reverted by RunDisableAuditLog, for the same reason the "Kernel Object"
// subcategory itself isn't turned back off there: this is a machine-wide setting that's also
// simply a widely-recommended baseline (Microsoft's own guidance is to enable it whenever using
// auditpol at all), so leaving it enabled after this feature is disabled is not a regression.
bool ForceAdvancedAuditPolicy()
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr
        );
    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] Failed to open HKLM\\SYSTEM\\CurrentControlSet\\Control\\Lsa: %ld\n", result);
        return false;
    }

    DWORD value = 1;
    result = RegSetValueExW(
        key, L"SCENoApplyLegacyAuditPolicy", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)
        );
    RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        wprintf(L"[ERROR] Failed to set SCENoApplyLegacyAuditPolicy: %ld\n", result);
        return false;
    }
    return true;
}

bool SetAuditSubcategory(const wchar_t* subcategoryGuid, bool enable)
{
    std::wstring arguments =
        L"/set /subcategory:" + std::wstring(subcategoryGuid) +
        L" /success:" + std::wstring(enable ? L"enable" : L"disable") +
        L" /failure:" + std::wstring(enable ? L"enable" : L"disable");

    int exitCode = RunSystem32Tool(L"auditpol.exe", arguments);
    if (exitCode != 0) {
        wprintf(L"[ERROR] auditpol.exe exited with code %d.\n", exitCode);
        return false;
    }
    return true;
}

bool SetObjectAccessAuditSubcategories(bool enable)
{
    if (enable && !ForceAdvancedAuditPolicy()) {
        return false;
    }

    // All three must be set: see FileSystemSubcategoryGuid's and
    // HandleManipulationSubcategoryGuid's comments above for why a control device opened by
    // name needs more than just "Kernel Object".
    bool ok = SetAuditSubcategory(KernelObjectSubcategoryGuid, enable);
    ok = SetAuditSubcategory(FileSystemSubcategoryGuid, enable) && ok;
    ok = SetAuditSubcategory(HandleManipulationSubcategoryGuid, enable) && ok;
    return ok;
}

// Builds the Scheduled Task definition XML (Task Scheduler 1.4 schema) for the reapply task:
// triggered at every system boot, running as SYSTEM, re-invoking this same executable with
// --apply-audit-sacl.
//
// This originally used an EventTrigger on Event ID 7036 ("service entered the running state"),
// but real-machine testing during this change found that 7036 is simply never logged for
// OpenInputBridgeKeyboard/OpenInputBridgeMouse: they're SERVICE_SYSTEM_START kernel drivers,
// loaded directly by the I/O Manager during early kernel-phase boot initialization, not started
// via the Service Control Manager's normal StartService() state machine the way ordinary Win32
// services are — so the SCM never observes an explicit "I started this" transition to log 7036
// for. (Confirmed via Get-WinEvent: the "Service Control Manager" provider logs plenty of other
// SCM event IDs like 7023/7026 on this machine, just never 7036 for these two services.) A
// BootTrigger sidesteps this entirely: these drivers load in the earliest phase of kernel init,
// long before Task Scheduler's own service starts and begins evaluating boot triggers, so by
// the time this task actually runs, the driver is guaranteed to already be up.
std::wstring BuildReapplyTaskXml(const std::wstring& exePath)
{
    return
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <RegistrationInfo>\n"
        L"    <Description>OpenInputBridge: reapplies the audit SACL on \\\\.\\interceptionNN "
        L"on every boot (the SACL does not survive control-device re-creation, and these "
        L"services are SERVICE_SYSTEM_START drivers that only (re)create their control devices "
        L"at boot). See docs/DECISIONS.md's 2026-08-09 entry.</Description>\n"
        L"  </RegistrationInfo>\n"
        L"  <Triggers>\n"
        L"    <BootTrigger>\n"
        L"      <Enabled>true</Enabled>\n"
        L"    </BootTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\">\n"
        L"      <UserId>S-1-5-18</UserId>\n" // SYSTEM — needs no interactive session (unlike the toast task).
        L"      <RunLevel>HighestAvailable</RunLevel>\n"
        L"    </Principal>\n"
        L"  </Principals>\n"
        L"  <Settings>\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n"
        L"    <Enabled>true</Enabled>\n"
        L"    <Hidden>true</Hidden>\n"
        L"    <ExecutionTimeLimit>PT1M</ExecutionTimeLimit>\n"
        L"  </Settings>\n"
        L"  <Actions Context=\"Author\">\n"
        L"    <Exec>\n"
        L"      <Command>\"" + exePath + L"\"</Command>\n"
        L"      <Arguments>--apply-audit-sacl</Arguments>\n"
        L"    </Exec>\n"
        L"  </Actions>\n"
        L"</Task>\n";
}

} // namespace

int RunEnableAuditLog()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    if (!EnablePrivilege(SE_SECURITY_NAME)) {
        wprintf(L"[ERROR] Failed to enable SeSecurityPrivilege -- cannot set an audit SACL.\n");
        return 1;
    }

    int appliedCount = ForEachControlDevice(true);
    if (appliedCount == 0) {
        // Expected right after a fresh driver install in the same session: install.cpp's
        // SetupInstallServicesFromInfSectionW call doesn't start the services (no
        // SPSVCINST_STARTSERVICE), and a SERVICE_SYSTEM_START driver isn't guaranteed to run
        // until the next boot — so no \\.\interceptionNN devices exist yet to set a SACL on.
        // Not fatal: the reapply Scheduled Task registered below (which does NOT depend on the
        // driver running) picks this up automatically on the next boot, via its own BootTrigger.
        // Contrast with RunApplyAuditSacl, where finding 0 devices genuinely is an error (it
        // only runs at boot, i.e. exactly when the driver should already be up).
        wprintf(L"No \\\\.\\interceptionNN devices are active yet (driver not running -- a reboot "
                L"may be required first). The SACL will be applied automatically once it starts.\n");
    } else {
        wprintf(L"Applied audit SACL to %d control device(s).\n", appliedCount);
    }

    if (!SetObjectAccessAuditSubcategories(true)) {
        return 1;
    }
    wprintf(L"Forced advanced audit policy to take precedence over legacy category policy, "
            L"and enabled the \"Kernel Object\", \"File System\", and \"Handle Manipulation\" "
            L"audit subcategories (success + failure).\n");

    if (!RegisterScheduledTaskFromXml(AuditLogReapplyTaskName, BuildReapplyTaskXml(GetInstallerExecutablePath()))) {
        return 1;
    }
    wprintf(L"Registered the '%s' Scheduled Task to reapply the SACL on every service start.\n", AuditLogReapplyTaskName);

    return 0;
}

int RunDisableAuditLog()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    if (!EnablePrivilege(SE_SECURITY_NAME)) {
        wprintf(L"[ERROR] Failed to enable SeSecurityPrivilege -- cannot clear the audit SACL.\n");
        return 1;
    }

    ForEachControlDevice(false);
    UnregisterScheduledTask(AuditLogReapplyTaskName);

    // Deliberately not disabling the "Kernel Object"/"File System"/"Handle Manipulation" auditpol subcategories
    // here — see auditlog.h's comment on RunDisableAuditLog.
    wprintf(L"Removed the audit SACL and the reapply Scheduled Task.\n");
    return 0;
}

int RunApplyAuditSacl()
{
    // No elevation/IsRunningElevated check here on purpose: this path is invoked by the
    // reapply Scheduled Task running as SYSTEM (which is always "elevated" in the sense that
    // matters — it already has SeSecurityPrivilege available), not interactively.
    if (!EnablePrivilege(SE_SECURITY_NAME)) {
        return 1;
    }

    return (ForEachControlDevice(true) > 0) ? 0 : 1;
}

int RunDumpAuditSacl()
{
    if (!IsRunningElevated()) {
        wprintf(L"[ERROR] This installer must be run as Administrator.\n");
        return 1;
    }

    // Reading a SACL requires ACCESS_SYSTEM_SECURITY on the open GetNamedSecurityInfoW performs
    // internally, same as writing one — see BuildAuditSacl's neighboring EnablePrivilege call.
    if (!EnablePrivilege(SE_SECURITY_NAME)) {
        wprintf(L"[ERROR] Failed to enable SeSecurityPrivilege -- cannot read the audit SACL.\n");
        return 1;
    }

    for (ULONG index = 0; index < TotalDeviceSlotCount; ++index) {
        DumpAuditSaclForDevice(BuildDevicePath(index));
    }
    return 0;
}

} // namespace OpenInputBridge
