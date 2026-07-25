// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.
//
// See common.h.

#include "common.h"

#include <string>
#include <vector>

namespace OpenInputBridge {

bool IsRunningElevated()
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedSize = 0;
    BOOL isElevated = FALSE;

    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returnedSize)) {
        isElevated = elevation.TokenIsElevated;
    }

    CloseHandle(token);
    return isElevated != FALSE;
}

namespace {

// Parses a REG_MULTI_SZ buffer (a sequence of null-terminated strings, itself terminated by
// an extra empty string / double null) into individual entries.
std::vector<std::wstring> ReadMultiSz(HKEY key, const wchar_t* valueName)
{
    std::vector<std::wstring> entries;
    DWORD type = 0;
    DWORD byteSize = 0;

    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteSize) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || byteSize < sizeof(wchar_t)) {
        return entries;
    }

    std::vector<wchar_t> buffer(byteSize / sizeof(wchar_t));

    if (RegQueryValueExW(
            key, valueName, nullptr, &type,
            reinterpret_cast<LPBYTE>(buffer.data()), &byteSize
            ) != ERROR_SUCCESS) {
        return entries;
    }

    const wchar_t* current = buffer.data();
    const wchar_t* end = buffer.data() + buffer.size();

    while (current < end && *current != L'\0') {
        std::wstring entry(current);
        current += entry.size() + 1;
        entries.push_back(std::move(entry));
    }

    return entries;
}

bool WriteMultiSz(HKEY key, const wchar_t* valueName, const std::vector<std::wstring>& entries)
{
    std::vector<wchar_t> buffer;

    for (const std::wstring& entry : entries) {
        buffer.insert(buffer.end(), entry.begin(), entry.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0'); // final double-null terminator, even for an empty list.

    LONG result = RegSetValueExW(
        key,
        valueName,
        0,
        REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(buffer.data()),
        static_cast<DWORD>(buffer.size() * sizeof(wchar_t))
        );

    return result == ERROR_SUCCESS;
}

bool EqualsCaseInsensitive(const std::wstring& a, const wchar_t* b)
{
    return _wcsicmp(a.c_str(), b) == 0;
}

} // namespace

bool AppendUpperFilter(const wchar_t* classRegistryPath, const wchar_t* entryName)
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, classRegistryPath, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    std::vector<std::wstring> entries = ReadMultiSz(key, UpperFiltersValueName);

    bool alreadyPresent = false;
    for (const std::wstring& entry : entries) {
        if (EqualsCaseInsensitive(entry, entryName)) {
            alreadyPresent = true;
            break;
        }
    }

    bool succeeded = true;
    if (!alreadyPresent) {
        entries.emplace_back(entryName);
        succeeded = WriteMultiSz(key, UpperFiltersValueName, entries);
    }

    RegCloseKey(key);
    return succeeded;
}

bool RemoveUpperFilter(const wchar_t* classRegistryPath, const wchar_t* entryName)
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, classRegistryPath, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        // Class key not present at all: nothing to remove, not an error.
        return true;
    }

    std::vector<std::wstring> entries = ReadMultiSz(key, UpperFiltersValueName);

    std::vector<std::wstring> filtered;
    filtered.reserve(entries.size());
    for (std::wstring& entry : entries) {
        if (!EqualsCaseInsensitive(entry, entryName)) {
            filtered.push_back(std::move(entry));
        }
    }

    bool succeeded = true;
    if (filtered.size() != entries.size()) {
        succeeded = WriteMultiSz(key, UpperFiltersValueName, filtered);
    }

    RegCloseKey(key);
    return succeeded;
}

} // namespace OpenInputBridge
