/******************************************************************************
*
* notepad420
*
* AclGuard.cpp
*   See AclGuard.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <string>
#include <vector>
#include "AclGuard.h"

namespace {

constexpr DWORD kSnapshotInfo =
	OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

/// Reads OWNER|GROUP|DACL of `path` as a self-relative descriptor (the form
/// GetFileSecurity produces, and the form SetFileSecurity consumes).
DWORD ReadFileSecurity(const std::wstring &path, std::vector<BYTE> &bytes) noexcept {
	bytes.clear();
	DWORD need = 0;
	GetFileSecurityW(path.c_str(), kSnapshotInfo, nullptr, 0, &need);
	if (need == 0) {
		return GetLastError();
	}
	bytes.resize(need);
	DWORD got = 0;
	if (!GetFileSecurityW(path.c_str(), kSnapshotInfo, bytes.data(), need, &got)) {
		const DWORD err = GetLastError();
		bytes.clear();
		return err;
	}
	bytes.resize(got);
	return ERROR_SUCCESS;
}

} // namespace

namespace AclGuard {

bool ValidateSddl(const wchar_t *sddl) noexcept {
	PSECURITY_DESCRIPTOR sd = nullptr;
	ULONG len = 0;
	const BOOL ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(
		sddl, SDDL_REVISION_1, &sd, &len);
	if (sd != nullptr) {
		LocalFree(sd);
	}
	return ok != FALSE;
}

std::wstring BuildReplaceSddl() noexcept {
	return kReplaceSddl;
}

std::wstring BackupPathFor(const std::wstring &path) {
	return path + L".bak";
}

std::wstring SnapshotPathFor(const std::wstring &path) {
	return path + L".bak.sd";
}

DWORD TakeOwnershipAndGrant(const std::wstring &path, std::wstring &error) noexcept {
	PSECURITY_DESCRIPTOR sd = nullptr;
	ULONG sdLen = 0;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			kReplaceSddl, SDDL_REVISION_1, &sd, &sdLen)) {
		error = L"内部错误:ACL 模板被系统拒绝(error=" + std::to_wstring(GetLastError()) + L")";
		return GetLastError();
	}
	PSID owner = nullptr;
	PSID group = nullptr;
	PACL dacl = nullptr;
	BOOL ownerDefaulted = FALSE;
	BOOL groupDefaulted = FALSE;
	BOOL daclPresent = FALSE;
	BOOL daclDefaulted = FALSE;
	if (!GetSecurityDescriptorOwner(sd, &owner, &ownerDefaulted) ||
		!GetSecurityDescriptorGroup(sd, &group, &groupDefaulted) ||
		!GetSecurityDescriptorDacl(sd, &daclPresent, &dacl, &daclDefaulted)) {
		LocalFree(sd);
		error = L"内部错误:无法解析 ACL 模板";
		return ERROR_INVALID_SECURITY_DESCR;
	}

	const DWORD ownerErr = SetNamedSecurityInfo(
		const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
		OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
		owner, group, nullptr, nullptr);
	DWORD daclErr = ERROR_SUCCESS;
	if (ownerErr == ERROR_SUCCESS) {
		daclErr = SetNamedSecurityInfo(
			const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);
	}
	LocalFree(sd);

	if (ownerErr != ERROR_SUCCESS) {
		error = L"设置属主失败(error=" + std::to_wstring(ownerErr) + L")";
		return ownerErr;
	}
	if (daclErr != ERROR_SUCCESS) {
		error = L"设置访问权限失败(error=" + std::to_wstring(daclErr) + L")";
		return daclErr;
	}
	return ERROR_SUCCESS;
}

DWORD SaveSecuritySnapshot(const std::wstring &path, const std::wstring &sdPath,
						   std::wstring &error) noexcept {
	if (PathFileExistsW(sdPath.c_str())) {
		return ERROR_SUCCESS;	// keep the first (true original) snapshot
	}
	PSECURITY_DESCRIPTOR sd = nullptr;
	std::vector<BYTE> bytes;
	const DWORD readErr = ReadFileSecurity(path, bytes);
	if (readErr != ERROR_SUCCESS) {
		error = L"读取原始权限失败(error=" + std::to_wstring(readErr) + L")";
		return readErr;
	}
	sd = bytes.data();
	if (!IsValidSecurityDescriptor(sd)) {
		error = L"原始权限不是有效的安全描述符";
		return ERROR_INVALID_SECURITY_DESCR;
	}
	const DWORD sdLen = static_cast<DWORD>(bytes.size());
	HANDLE hFile = CreateFileW(sdPath.c_str(), GENERIC_WRITE, 0, nullptr,
							   CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		error = L"写入权限快照失败(error=" + std::to_wstring(err) + L")";
		return err;
	}
	DWORD written = 0;
	const BOOL ok = WriteFile(hFile, bytes.data(), sdLen, &written, nullptr);
	const DWORD writeErr = ok ? ERROR_SUCCESS : GetLastError();
	CloseHandle(hFile);
	if (!ok || written != sdLen) {
		error = L"权限快照写入不完整(error=" + std::to_wstring(writeErr) + L")";
		return writeErr == ERROR_SUCCESS ? ERROR_WRITE_FAULT : writeErr;
	}
	return ERROR_SUCCESS;
}

DWORD RestoreSecuritySnapshot(const std::wstring &path, const std::wstring &sdPath,
							  std::wstring &error) noexcept {
	HANDLE hFile = CreateFileW(sdPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
							   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		error = L"读取权限快照失败(error=" + std::to_wstring(err) + L")";
		return err;
	}
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > 0x10000) {
		CloseHandle(hFile);
		error = L"权限快照内容异常";
		return ERROR_INVALID_DATA;
	}
	std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));
	DWORD read = 0;
	const BOOL ok = ReadFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
	CloseHandle(hFile);
	if (!ok || read != bytes.size()) {
		error = L"权限快照读取不完整";
		return ERROR_READ_FAULT;
	}
	if (!IsValidSecurityDescriptor(bytes.data())) {
		error = L"权限快照不是有效的安全描述符";
		return ERROR_INVALID_SECURITY_DESCR;
	}
	if (!SetFileSecurityW(path.c_str(), kSnapshotInfo, bytes.data())) {
		const DWORD err = GetLastError();
		error = L"回写原始权限失败(error=" + std::to_wstring(err) + L")";
		return err;
	}
	return ERROR_SUCCESS;
}

DWORD CreateShortcut(const std::wstring &lnkPath, const std::wstring &targetPath,
					 std::wstring &error) noexcept {
	IShellLinkW *link = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
								  IID_IShellLinkW, reinterpret_cast<void **>(&link));
	if (FAILED(hr) || link == nullptr) {
		error = L"创建快捷方式失败(COM error=0x" + std::to_wstring(static_cast<unsigned long>(hr)) + L")";
		return static_cast<DWORD>(hr);
	}
	link->SetPath(targetPath.c_str());
	link->SetWorkingDirectory(targetPath.substr(0, targetPath.find_last_of(L'\\')).c_str());

	IPersistFile *file = nullptr;
	hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));
	if (SUCCEEDED(hr) && file != nullptr) {
		hr = file->Save(lnkPath.c_str(), TRUE);
		file->Release();
	}
	link->Release();
	if (FAILED(hr)) {
		error = L"保存快捷方式失败(COM error=0x" + std::to_wstring(static_cast<unsigned long>(hr)) + L")";
		return static_cast<DWORD>(hr);
	}
	return ERROR_SUCCESS;
}

namespace {

/// Recursively copies `srcDir` into `destDir`, creating directories and
/// overwriting files. Returns false on the first failed operation.
bool CopyTree(const std::wstring &srcDir, const std::wstring &destDir,
              std::wstring &error) {
    if (!CreateDirectoryW(destDir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        error = L"创建目录失败 " + destDir + L" (error=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = srcDir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return true;	// empty source tree: nothing to copy
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        const std::wstring s = srcDir + L"\\" + fd.cFileName;
        const std::wstring d = destDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyTree(s, d, error)) {
                FindClose(h);
                return false;
            }
        } else if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) {
            error = L"复制 " + s + L" 失败 (error=" + std::to_wstring(GetLastError()) + L")";
            FindClose(h);
            return false;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

/// Resolves the dir that hosts `targetPath`, e.g. C:\Windows\System32.
std::wstring ParentDirOf(const std::wstring &path) {
    const size_t pos = path.find_last_of(L'\\');
    return pos == std::wstring::npos ? path : path.substr(0, pos);
}

/// Deletes every entry under `destLocaleDir` whose name matches an entry in
/// `sourceLocaleDir` (what we know we deployed), recursively. Tolerates
/// entries that are already gone.
bool RemoveDeployedTree(const std::wstring &sourceLocaleDir,
                        const std::wstring &destLocaleDir,
                        std::wstring &error) {
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = sourceLocaleDir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return true;	// no source tree: nothing we know we deployed
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        const std::wstring s = sourceLocaleDir + L"\\" + fd.cFileName;
        const std::wstring d = destLocaleDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (PathIsDirectoryW(d.c_str())) {
                if (!RemoveDeployedTree(s, d, error)) {
                    FindClose(h);
                    return false;
                }
                if (!RemoveDirectoryW(d.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                    error = L"删除目录失败 " + d + L" (error=" + std::to_wstring(GetLastError()) + L")";
                    FindClose(h);
                    return false;
                }
            }
        } else {
            if (!DeleteFileW(d.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
                error = L"删除文件失败 " + d + L" (error=" + std::to_wstring(GetLastError()) + L")";
                FindClose(h);
                return false;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

} // namespace

bool DeployLocaleBeside(const std::wstring &targetPath,
                        const std::wstring &sourceLocaleDir,
                        std::wstring &error) noexcept {
    try {
        const std::wstring dest = ParentDirOf(targetPath) + L"\\locale";
        return CopyTree(sourceLocaleDir, dest, error);
    } catch (...) {
        error = L"locale 部署内部异常";
        return false;
    }
}

bool RemoveLocaleBeside(const std::wstring &targetPath,
                        const std::wstring &sourceLocaleDir,
                        std::wstring &error) noexcept {
    try {
        const std::wstring dest = ParentDirOf(targetPath) + L"\\locale";
        if (!RemoveDeployedTree(sourceLocaleDir, dest, error)) {
            return false;
        }
        // Tidy up: drop the now-empty `locale` dir if we created the only
        // thing left in it.
        RemoveDirectoryW(dest.c_str());	// fails harmlessly if not empty
        return true;
    } catch (...) {
        error = L"locale 回收内部异常";
        return false;
    }
}


namespace {

constexpr wchar_t kEnvKey[] = L"Environment";

// Per-variable backup slots under backupRegKey: <prefix>Present (DWORD) and
// <prefix>Value (REG_SZ).
struct VarSlot {
    const wchar_t *name;         // EDITOR / VISUAL
    const wchar_t *backupPresent;
    const wchar_t *backupValue;
};

void BroadcastEnvChange() noexcept {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 2000, &result);
}

DWORD ReadOne(const wchar_t *name, std::wstring &value, bool &present) {
    value.clear();
    present = false;
    DWORD type = 0, size = 0;
    LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, kEnvKey, name,
                             RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                             &type, nullptr, &size);
    if (s == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (s != ERROR_SUCCESS) {
        return static_cast<DWORD>(s);
    }
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    s = RegGetValueW(HKEY_CURRENT_USER, kEnvKey, name,
                     RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     &type, buf.data(), &size);
    if (s != ERROR_SUCCESS) {
        return static_cast<DWORD>(s);
    }
    value = buf.data();
    present = true;
    return ERROR_SUCCESS;
}

DWORD BackupVarIfUnset(HKEY backupKey, const VarSlot &slot) {
    if (RegQueryValueExW(backupKey, slot.backupPresent, nullptr, nullptr, nullptr, nullptr)
            != ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    std::wstring prev;
    bool present = false;
    DWORD rc = ReadOne(slot.name, prev, present);
    if (rc != ERROR_SUCCESS) {
        return rc;
    }
    const DWORD flag = present ? 1u : 0u;
    RegSetValueExW(backupKey, slot.backupPresent, 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&flag), sizeof(flag));
    if (present) {
        RegSetValueExW(backupKey, slot.backupValue, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(prev.c_str()),
                       static_cast<DWORD>((prev.size() + 1) * sizeof(wchar_t)));
    }
    return ERROR_SUCCESS;
}

DWORD RestoreVar(const std::wstring &backupRegKey, const VarSlot &slot,
                 std::wstring &error) {
    DWORD type = 0, size = sizeof(DWORD), present = 0;
    LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, backupRegKey.c_str(),
                             slot.backupPresent, RRF_RT_REG_DWORD, &type,
                             &present, &size);
    if (s == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;	// we never wrote this var
    }
    if (s != ERROR_SUCCESS) {
        error = std::wstring(L"读取 ") + slot.name + L" 备份失败(error=" + std::to_wstring(s) + L")";
        return static_cast<DWORD>(s);
    }
    HKEY env = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kEnvKey, 0, nullptr, 0,
                             KEY_SET_VALUE, nullptr, &env, nullptr);
    if (r != ERROR_SUCCESS) {
        error = std::wstring(L"无法写入用户环境键(error=") + std::to_wstring(r) + L")";
        return static_cast<DWORD>(r);
    }
    if (present) {
        wchar_t buf[4096];
        DWORD bsize = sizeof(buf);
        s = RegGetValueW(HKEY_CURRENT_USER, backupRegKey.c_str(),
                         slot.backupValue, RRF_RT_REG_SZ, &type, buf, &bsize);
        if (s == ERROR_SUCCESS) {
            RegSetValueExW(env, slot.name, 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(buf), bsize);
        }
    } else {
        RegDeleteValueW(env, slot.name);
    }
    RegCloseKey(env);
    
    return ERROR_SUCCESS;
}

} // namespace

DWORD ReadUserEnvValue(const wchar_t *name, std::wstring &value, bool &present) noexcept {
    try {
        return ReadOne(name, value, present);
    } catch (...) {
        return ERROR_INVALID_PARAMETER;
    }
}

DWORD SetUserEditors(const std::wstring &editorPath,
                     const std::wstring &backupRegKey,
                     std::wstring &error) noexcept {
    try {
        HKEY hKey = nullptr;
        LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, backupRegKey.c_str(), 0, nullptr, 0,
                                 KEY_READ | KEY_SET_VALUE, nullptr, &hKey, nullptr);
        if (r != ERROR_SUCCESS) {
            error = L"无法创建备份注册表键(error=" + std::to_wstring(r) + L")";
            return static_cast<DWORD>(r);
        }
        const VarSlot slots[] = {
            { L"EDITOR", L"EditorPresent", L"EditorValue" },
            { L"VISUAL", L"VisualPresent", L"VisualValue" },
        };
        for (const auto &slot : slots) {
            const DWORD rc = BackupVarIfUnset(hKey, slot);
            if (rc != ERROR_SUCCESS) {
                RegCloseKey(hKey);
                error = std::wstring(L"备份 ") + slot.name + L" 失败(error=" + std::to_wstring(rc) + L")";
                return rc;
            }
            // write the new value
            HKEY env = nullptr;
            r = RegCreateKeyExW(HKEY_CURRENT_USER, kEnvKey, 0, nullptr, 0,
                                KEY_SET_VALUE, nullptr, &env, nullptr);
            if (r != ERROR_SUCCESS) {
                RegCloseKey(hKey);
                error = L"无法写入用户环境键(error=" + std::to_wstring(r) + L")";
                return static_cast<DWORD>(r);
            }
            r = RegSetValueExW(env, slot.name, 0, REG_SZ,
                               reinterpret_cast<const BYTE *>(editorPath.c_str()),
                               static_cast<DWORD>((editorPath.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(env);
            if (r != ERROR_SUCCESS) {
                RegCloseKey(hKey);
                error = std::wstring(L"写入 ") + slot.name + L" 失败(error=" + std::to_wstring(r) + L")";
                return static_cast<DWORD>(r);
            }
        }
        RegCloseKey(hKey);
        BroadcastEnvChange();
        return ERROR_SUCCESS;
    } catch (...) {
        error = L"env 写入内部异常";
        return ERROR_INVALID_PARAMETER;
    }
}

DWORD RestoreUserEditors(const std::wstring &backupRegKey,
                         std::wstring &error) noexcept {
    try {
        HKEY hKey = nullptr;
        LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, backupRegKey.c_str(), 0, KEY_READ, &hKey);
        if (r == ERROR_FILE_NOT_FOUND) {
            return ERROR_SUCCESS;
        }
        if (r != ERROR_SUCCESS) {
            error = L"无法打开备份注册表键(error=" + std::to_wstring(r) + L")";
            return static_cast<DWORD>(r);
        }
        RegCloseKey(hKey);
        const VarSlot slots[] = {
            { L"EDITOR", L"EditorPresent", L"EditorValue" },
            { L"VISUAL", L"VisualPresent", L"VisualValue" },
        };
        for (const auto &slot : slots) {
            const DWORD rc = RestoreVar(backupRegKey, slot, error);
            if (rc != ERROR_SUCCESS) {
                return rc;
            }
        }
        RegDeleteKeyW(HKEY_CURRENT_USER, backupRegKey.c_str());
        BroadcastEnvChange();
        return ERROR_SUCCESS;
    } catch (...) {
        error = L"env 回退内部异常";
        return ERROR_INVALID_PARAMETER;
    }
}

} // namespace AclGuard