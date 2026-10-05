// default-open takeover: hash algorithm + registry actions.
// The hash is a byte-exact port of DanysysTeam/PS-SFTA Get-Hash, validated
// against the 25 real NP3 UserChoice entries on the author's machine
// (golden vectors in tools/tests/replacer_hash_test.cpp).
#include "DefaultApp.h"
#include <sddl.h>
#include <string_view>

#include <algorithm>

#pragma comment(lib, "advapi32.lib")

namespace DefaultApp {

const wchar_t kExperienceFallback[] =
	L"User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}";
const wchar_t kOurProgId[] = L"Applications\\notepad420.exe";
const wchar_t kLegacyProgId[] = L"Applications\\notepad.exe";	// 同一琉墨下批 011-「默认打开方式」统一改为这三个 (ADR 0006)

// ADR 0010 的默认打开接管清单:011 时代本机 25 条真实 NP3 接管项。
// 测试(tools/tests/replacer_cli_test.cpp)与本实现同源引用此常量。
const wchar_t *const kAssociateExts[] = {
	L".cfg", L".conf", L".config", L".domains", L".dmp",
	L".editorconfig", L".gitattributes", L".gitconfig", L".gitignore", L".gitmodules",
	L".glsl", L".ini", L".json", L".json5", L".jsonc", L".jsonl",
	L".log", L".lua", L".md", L".nip",
	L".properties", L".toml", L".txt", L".xml", L".yaml",
};
const size_t kAssociateExtsCount = sizeof(kAssociateExts) / sizeof(kAssociateExts[0]);

namespace {

constexpr wchar_t kIfeoSubKey[] =
	L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe";
constexpr wchar_t kDefaultFileExtsSubKey[] =
	L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts";

// 测试可重定向的 FileExts 根子键(默认 = 真实 FileExts;见 SetFileExtsSubKeyForTesting)。
std::wstring g_fileExtsSubKey = kDefaultFileExtsSubKey;
// 测试可重定向的 Classes 根子键(ProgId 写入/删除用;见 SetClassesSubKeyForTesting)。
std::wstring g_classesSubKey = L"Software\\Classes";

// ---- MD5 (local; no crypto lib linked) ------------------------------------

namespace md5 {

struct Ctx {
	uint32_t a = 0x67452301, b = 0xEFCDAB89, c = 0x98BADCFE, d = 0x10325476;
	uint64_t len = 0;
	uint8_t buf[64]{};
	size_t bufLen = 0;
};

inline uint32_t rotl(uint32_t x, uint32_t n) noexcept { return (x << n) | (x >> (32 - n)); }

void block(Ctx &c, const uint8_t *p) noexcept {
	static const uint32_t K[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	static const uint32_t S[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
	};
	uint32_t m[16];
	for (int i = 0; i < 16; ++i) {
		memcpy(&m[i], p + 4 * i, 4);
	}
	uint32_t a = c.a, b = c.b, cc = c.c, d = c.d;
	for (int i = 0; i < 64; ++i) {
		uint32_t f, g;
		if (i < 16) { f = (b & cc) | (~b & d); g = (uint32_t)i; }
		else if (i < 32) { f = (d & b) | (~d & cc); g = (5u * i + 1) & 15; }
		else if (i < 48) { f = b ^ cc ^ d; g = (3u * i + 5) & 15; }
		else { f = cc ^ (b | ~d); g = (7u * i) & 15; }
		const uint32_t tmp = d;
		d = cc; cc = b;
		b = b + rotl(a + f + K[i] + m[g], S[i]);
		a = tmp;
	}
	c.a += a; c.b += b; c.c += cc; c.d += d;
}

void update(Ctx &c, const uint8_t *p, size_t n) noexcept {
	c.len += n;
	while (n) {
		const size_t take = (std::min)(n, 64 - c.bufLen);
		memcpy(c.buf + c.bufLen, p, take);
		c.bufLen += take; p += take; n -= take;
		if (c.bufLen == 64) { block(c, c.buf); c.bufLen = 0; }
	}
}

void finish(Ctx &c, uint8_t out[16]) noexcept {
	const uint64_t bitlen = c.len * 8;
	uint8_t pad = 0x80;
	update(c, &pad, 1);
	uint8_t zero = 0;
	while (c.bufLen != 56) { update(c, &zero, 1); }
	uint8_t lb[8];
	for (int i = 0; i < 8; ++i) lb[i] = (uint8_t)(bitlen >> (8 * i));
	update(c, lb, 8);
	const uint32_t words[4] = {c.a, c.b, c.c, c.d};
	for (int i = 0; i < 4; ++i) memcpy(out + 4 * i, &words[i], 4);
}

} // namespace md5

// ---- hash building blocks -------------------------------------------------

int32_t I32(int64_t v) noexcept { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

// Get-ShiftRight: arithmetic shift-right over the low 32 bits.
int32_t ShiftR(int64_t v, int c) noexcept {
	const int32_t x = I32(v);
	if (x & 0x80000000) {
		// keep the sign-filled high bits, then flip the top 16 like PS's bxor
		const int32_t shifted = x >> c;	// MSVC: arithmetic shift
		return I32(static_cast<uint32_t>(shifted) ^ 0xFFFF0000u);
	}
	return x >> c;
}

int32_t LongAt(const uint8_t *b, size_t i) noexcept {
	int32_t v = 0;
	memcpy(&v, b + i, 4);	// little-endian, signed
	return v;
}

std::string Base64(const uint8_t *d, size_t n) {
	static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string s;
	for (size_t i = 0; i < n; i += 3) {
		const uint32_t v = (uint32_t(d[i]) << 16) |
			(i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < n ? uint32_t(d[i + 2]) : 0);
		s += t[(v >> 18) & 63];
		s += t[(v >> 12) & 63];
		s += (i + 1 < n) ? t[(v >> 6) & 63] : '=';
		s += (i + 2 < n) ? t[v & 63] : '=';
	}
	return s;
}

// First mixing pass (ulates outhash1/outhash2 for outHash[0..8)).
void Loop1(int64_t md51, int64_t md52, const uint8_t *bb, int64_t counter0,
		   int64_t &oh1out, int64_t &oh2out) noexcept {
	int64_t cache = 0, oh1 = 0;
	size_t pd = 0;
	for (int64_t counter = counter0; counter; --counter) {
		const int64_t r0 = I32(LongAt(bb, pd) + oh1);
		const int64_t r1 = I32(LongAt(bb, pd + 4));
		pd += 8;
		const int64_t r2_0 = I32(r0 * md51 - INT64_C(0x10FA9605) * ShiftR(r0, 16));
		const int64_t r2_1 = I32(INT64_C(0x79F8A395) * r2_0 + INT64_C(0x689B6B9F) * ShiftR(r2_0, 16));
		const int64_t r3 = I32(INT64_C(0xEA970001) * r2_1 - INT64_C(0x3C101569) * ShiftR(r2_1, 16));
		const int64_t r4_0 = I32(r3 + r1);
		const int64_t r5_0 = I32(cache + r3);
		const int64_t r6_0 = I32(r4_0 * md52 - INT64_C(0x3CE8EC25) * ShiftR(r4_0, 16));
		const int64_t r6_1 = I32(INT64_C(0x59C3AF2D) * r6_0 - INT64_C(0x2232E0F1) * ShiftR(r6_0, 16));
		oh1 = I32(INT64_C(0x1EC90001) * r6_1 + INT64_C(0x35BD1EC9) * ShiftR(r6_1, 16));
		const int64_t oh2 = I32(r5_0 + oh1);
		cache = oh2;
	}
	oh1out = oh1;
	oh2out = cache;
}

// Second mixing pass (fills outHash[8..16)).
void Loop2(int64_t md51, int64_t md52, const uint8_t *bb, int64_t counter0,
		   int64_t &oh1out, int64_t &oh2out) noexcept {
	int64_t cache = 0, oh1 = 0;
	size_t pd = 0;
	for (int64_t counter = counter0; counter; --counter) {
		const int64_t r0 = I32(LongAt(bb, pd) + oh1);
		pd += 8;
		const int64_t r1_0 = I32(r0 * md51);
		const int64_t r1_1 = I32(INT64_C(0xB1110000) * r1_0 - INT64_C(0x30674EEF) * ShiftR(r1_0, 16));
		const int64_t r2_0 = I32(INT64_C(0x5B9F0000) * r1_1 - INT64_C(0x78F7A461) * ShiftR(r1_1, 16));
		const int64_t r2_1 = I32(INT64_C(0x12CEB96D) * ShiftR(r2_0, 16) - INT64_C(0x46930000) * r2_0);
		const int64_t r3 = I32(INT64_C(0x1D830000) * r2_1 + INT64_C(0x257E1D83) * ShiftR(r2_1, 16));
		const int64_t r4_0 = I32(md52 * (r3 + I32(LongAt(bb, pd - 4))));
		const int64_t r4_1 = I32(INT64_C(0x16F50000) * r4_0 - INT64_C(0x5D8BE90B) * ShiftR(r4_0, 16));
		const int64_t r5_0 = I32(INT64_C(0x96FF0000) * r4_1 - INT64_C(0x2C7C6901) * ShiftR(r4_1, 16));
		const int64_t r5_1 = I32(INT64_C(0x2B890000) * r5_0 + INT64_C(0x7C932B89) * ShiftR(r5_0, 16));
		oh1 = I32(INT64_C(0x9F690000) * r5_1 - INT64_C(0x405B6097) * ShiftR(r5_1, 16));
		const int64_t oh2 = I32(oh1 + cache + r3);
		cache = oh2;
	}
	oh1out = oh1;
	oh2out = cache;
}

} // namespace

} // namespace DefaultApp

namespace DefaultApp {

namespace {

constexpr uint64_t kTicksPerMinute = 600000000ULL;	// 100ns ticks

uint64_t FileTimeAsU64(const FILETIME &ft) noexcept {
	return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

uint64_t TruncateToMinute(uint64_t ft) noexcept { return ft - (ft % kTicksPerMinute); }

// hi+lo 8-digit lowercase hex, matching SFTA Get-HexDateTime
std::wstring HexDateTimeFromFileTime(uint64_t ft) {
	wchar_t buf[32];
	swprintf(buf, 32, L"%08x%08x",
			 static_cast<uint32_t>(ft >> 32), static_cast<uint32_t>(ft));
	return buf;
}

bool KeyLastWriteTime(HKEY hKey, uint64_t &out) {
	FILETIME ft{};
	const LSTATUS s = RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr,
									   nullptr, nullptr, nullptr, nullptr, nullptr,
									   nullptr, &ft);
	if (s != ERROR_SUCCESS) {
		return false;
	}
	out = FileTimeAsU64(ft);
	return true;
}

// Enumerate FileExts exts that have a UserChoice\ProgId value.
template <typename F> void ForEachUserChoiceProgId(F &&fn) {
	HKEY fe = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, g_fileExtsSubKey.c_str(), 0, KEY_READ, &fe) != ERROR_SUCCESS) {
		return;
	}
	DWORD idx = 0;
	for (;;) {
		wchar_t ext[256];
		DWORD len = static_cast<DWORD>(std::size(ext));
		const LSTATUS s = RegEnumKeyExW(fe, idx, ext, &len, nullptr, nullptr, nullptr, nullptr);
		if (s != ERROR_SUCCESS) {
			break;
		}
		++idx;
		const std::wstring ucKey = g_fileExtsSubKey + L"\\" + ext + L"\\UserChoice";
		wchar_t progid[512];
		DWORD plen = sizeof(progid);
		if (RegGetValueW(HKEY_CURRENT_USER, ucKey.c_str(), L"ProgId", RRF_RT_REG_SZ,
						 nullptr, progid, &plen) == ERROR_SUCCESS) {
			fn(std::wstring(ext), std::wstring(progid));
		}
	}
	RegCloseKey(fe);
}

bool DeleteUserChoiceKey(const wchar_t *ext) {
	const std::wstring ucKey = g_fileExtsSubKey + L"\\" + ext + L"\\UserChoice";
	const LSTATUS s = RegDeleteKeyW(HKEY_CURRENT_USER, ucKey.c_str());
	return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
}

} // namespace

bool IsNotepad3Target(const wchar_t *value) noexcept {
	if (value == nullptr) {
		return false;
	}
	std::wstring v(value);
	for (auto &ch : v) { ch = static_cast<wchar_t>(towlower(ch)); }
	return v.find(L"notepad3") != std::wstring::npos;
}

std::wstring ReadUserExperienceString() {
	wchar_t path[MAX_PATH];
	if (!ExpandEnvironmentStringsW(L"%SystemRoot%\\SysWOW64\\shell32.dll", path, MAX_PATH)) {
		return kExperienceFallback;
	}
	const HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
								 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return kExperienceFallback;
	}
	const DWORD size = GetFileSize(h, nullptr);
	std::vector<uint8_t> data(size);
	DWORD got = 0;
	const BOOL ok = ReadFile(h, data.data(), size, &got, nullptr);
	CloseHandle(h);
	if (!ok || got < 2) {
		return kExperienceFallback;
	}
	const wchar_t *text = reinterpret_cast<const wchar_t *>(data.data());
	const std::wstring_view view(text, got / 2);
	const wchar_t needle[] = L"User Choice set via Windows User Experience";
	const size_t pos = view.find(needle);
	if (pos == std::wstring_view::npos) {
		return kExperienceFallback;
	}
	const size_t end = view.find(L'}', pos);
	if (end == std::wstring_view::npos) {
		return kExperienceFallback;
	}
	return std::wstring(text + pos, end - pos + 1);
}

std::wstring GetCurrentUserSid() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return std::wstring();
	}
	uint8_t buf[512];
	DWORD need = 0;
	std::wstring sid;
	if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &need)) {
		const auto *tu = reinterpret_cast<TOKEN_USER *>(buf);
		LPWSTR str = nullptr;
		if (ConvertSidToStringSidW(tu->User.Sid, &str)) {
			sid = str;
			LocalFree(str);
		}
	}
	CloseHandle(token);
	return sid;
}

std::wstring ComputeUserChoiceHash(const std::wstring &ext, const std::wstring &sid,
								   const std::wstring &progId, const std::wstring &hexDateTime,
								   const std::wstring &experience) {
	std::wstring baseInfo = ext + sid + progId + hexDateTime + experience;
	for (auto &ch : baseInfo) { ch = static_cast<wchar_t>(towlower(ch)); }

	// UTF-16LE bytes + two trailing zero bytes (string terminator)
	std::vector<uint8_t> bytes(baseInfo.size() * 2 + 2, 0);
	memcpy(bytes.data(), baseInfo.data(), baseInfo.size() * 2);

	uint8_t digest[16];
	{
		md5::Ctx ctx;
		md5::update(ctx, bytes.data(), bytes.size());
		md5::finish(ctx, digest);
	}

	const int64_t lengthBase = static_cast<int64_t>(baseInfo.size() * 2) + 2;
	const int64_t addOne = ((lengthBase & 4) <= 1) ? 1 : 0;
	const int64_t length = addOne + (lengthBase >> 2) - 1;
	if (length <= 1) {
		return std::wstring();
	}
	const int64_t counter0 = ((length - 2) >> 1) + 1;

	const int64_t m0 = static_cast<int64_t>(LongAt(digest, 0)) | 1;
	const int64_t m1 = static_cast<int64_t>(LongAt(digest, 4)) | 1;

	int64_t a = 0, b = 0, c = 0, d = 0;
	Loop1(m0 + 0x69FB0000LL, m1 + 0x13DB0000LL, bytes.data(), counter0, a, b);
	Loop2(m0, m1, bytes.data(), counter0, c, d);

	const int32_t hv1 = I32(c) ^ I32(a);
	const int32_t hv2 = I32(d) ^ I32(b);
	uint8_t raw[8];
	memcpy(raw, &hv1, 4);
	memcpy(raw + 4, &hv2, 4);
	const std::string b64 = Base64(raw, 8);
	return std::wstring(b64.begin(), b64.end());
}

bool WriteUserChoice(const wchar_t *ext, const wchar_t *progId, std::wstring &error) {
	if (!DeleteUserChoiceKey(ext)) {
		error = std::wstring(ext) + L": cannot remove existing UserChoice";
		return false;
	}
	const std::wstring sid = GetCurrentUserSid();
	if (sid.empty()) {
		error = L"GetCurrentUserSid failed";
		return false;
	}
	const std::wstring experience = ReadUserExperienceString();
	const std::wstring ucKey = g_fileExtsSubKey + L"\\" + ext + L"\\UserChoice";

	for (int attempt = 0; attempt < 3; ++attempt) {
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, ucKey.c_str(), 0, nullptr, 0,
							KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
			error = std::wstring(L"cannot create ") + ucKey;
			return false;
		}
		uint64_t ft = 0;
		const bool gotFt = KeyLastWriteTime(hKey, ft);
		RegSetValueExW(hKey, L"ProgId", 0, REG_SZ,
					   reinterpret_cast<const BYTE *>(progId),
					   static_cast<DWORD>((wcslen(progId) + 1) * sizeof(wchar_t)));
		const std::wstring hxdt = HexDateTimeFromFileTime(TruncateToMinute(ft));
		const std::wstring hash = ComputeUserChoiceHash(ext, sid, progId, hxdt, experience);
		if (hash.empty()) {
			RegCloseKey(hKey);
			error = L"hash computation returned empty";
			return false;
		}
		RegSetValueExW(hKey, L"Hash", 0, REG_SZ,
					   reinterpret_cast<const BYTE *>(hash.c_str()),
					   static_cast<DWORD>((hash.size() + 1) * sizeof(wchar_t)));
		uint64_t ft2 = 0;
		const bool gotFt2 = KeyLastWriteTime(hKey, ft2);
		RegCloseKey(hKey);
		if (gotFt && gotFt2 && TruncateToMinute(ft) == TruncateToMinute(ft2)) {
			return true;
		}
		// minute boundary flipped mid-write; recompute with the new timestamp
	}
	error = std::wstring(ext) + L": minute boundary flip, retry exhausted";
	return false;
}

ActionReport RemoveNp3IfeoHijacks() {
	ActionReport r;
	for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
		HKEY hKey = nullptr;
		const LSTATUS openStatus = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIfeoSubKey, 0,
												 KEY_QUERY_VALUE | view, &hKey);
		if (openStatus == ERROR_FILE_NOT_FOUND) {
			continue;
		}
		if (openStatus != ERROR_SUCCESS) {
			r.error = L"IFEO open failed(" + std::to_wstring(openStatus) + L")";
			return r;
		}
		wchar_t dbg[1024];
		DWORD dlen = sizeof(dbg);
		const LSTATUS g = RegGetValueW(hKey, nullptr, L"Debugger",
									   RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
									   nullptr, dbg, &dlen);
		RegCloseKey(hKey);
		if (g == ERROR_FILE_NOT_FOUND) {
			continue;	// no Debugger value, not hijacked
		}
		if (g != ERROR_SUCCESS) {
			r.error = L"IFEO Debugger read failed(" + std::to_wstring(g) + L")";
			return r;
		}
		if (!IsNotepad3Target(dbg)) {
			r.skipped.push_back(std::wstring(L"IFEO Debugger points at non-Notepad3: ") + dbg);
			continue;
		}
		const LSTATUS del = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kIfeoSubKey, view, 0);
		if (del == ERROR_FILE_NOT_FOUND) {
			continue;
		}
		if (del != ERROR_SUCCESS) {
			r.error = L"IFEO delete failed(" + std::to_wstring(del) + L")";
			return r;
		}
		++r.deleted;
	}
	return r;
}

ActionReport RewriteNp3UserChoices() {
	ActionReport r;
	std::vector<std::wstring> hits;
	ForEachUserChoiceProgId([&](std::wstring ext, std::wstring progid) {
		if (IsNotepad3Target(progid.c_str()) ||
				_wcsicmp(progid.c_str(), kLegacyProgId) == 0) {
			hits.push_back(std::move(ext));
		}
	});
	for (const auto &ext : hits) {
		std::wstring error;
		if (!WriteUserChoice(ext.c_str(), kOurProgId, error)) {
			r.error = ext + L": " + error;
			return r;
		}
		++r.rewritten;
	}
	return r;
}

ActionReport CleanupOurUserChoices() {
	ActionReport r;
	std::vector<std::wstring> hits;
	ForEachUserChoiceProgId([&](std::wstring ext, std::wstring progid) {
		if (_wcsicmp(progid.c_str(), kOurProgId) == 0 ||
				_wcsicmp(progid.c_str(), kLegacyProgId) == 0) {
			hits.push_back(std::move(ext));
		}
	});
	for (const auto &ext : hits) {
		if (!DeleteUserChoiceKey(ext.c_str())) {
			r.error = ext + L": delete failed";
			return r;
		}
		++r.deleted;
	}
	return r;
}


void SetFileExtsSubKeyForTesting(const wchar_t *subKey) {
	g_fileExtsSubKey = (subKey != nullptr) ? subKey : kDefaultFileExtsSubKey;
}

void SetClassesSubKeyForTesting(const wchar_t *subKey) {
	g_classesSubKey = (subKey != nullptr) ? subKey : L"Software\\Classes";
}

DWORD WriteOurProgId(const std::wstring &exePath, std::wstring &error) noexcept {
    const std::wstring key = g_classesSubKey + L"\\" + std::wstring(kOurProgId);
    HKEY hKey = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
                             KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) {
        error = L"创建 ProgId 失败(error=" + std::to_wstring(r) + L")";
        return static_cast<DWORD>(r);
    }
    const std::wstring cmd = L"\"" + exePath + L"\" \"%1\"";
    const std::wstring ico = exePath + L",0";
    LONG r1 = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                             reinterpret_cast<const BYTE *>(L""), sizeof(wchar_t) * 2);
    RegCloseKey(hKey);

    HKEY hCmd = nullptr;
    r = RegCreateKeyExW(HKEY_CURRENT_USER, (key + L"\\shell\\open\\command").c_str(), 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &hCmd, nullptr);
    if (r == ERROR_SUCCESS) {
        r = RegSetValueExW(hCmd, nullptr, 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hCmd);
    }
    if (r != ERROR_SUCCESS) {
        error = L"写入 shell\\open\\command 失败(error=" + std::to_wstring(r) + L")";
        return static_cast<DWORD>(r);
    }
    HKEY hIco = nullptr;
    r = RegCreateKeyExW(HKEY_CURRENT_USER, (key + L"\\DefaultIcon").c_str(), 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &hIco, nullptr);
    if (r == ERROR_SUCCESS) {
        r = RegSetValueExW(hIco, nullptr, 0, REG_EXPAND_SZ,
                           reinterpret_cast<const BYTE *>(ico.c_str()),
                           static_cast<DWORD>((ico.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hIco);
    }
    if (r != ERROR_SUCCESS) {
        error = L"写入 DefaultIcon 失败(error=" + std::to_wstring(r) + L")";
        return static_cast<DWORD>(r);
    }
    (void)r1;
    return ERROR_SUCCESS;
}

DWORD DeleteOurProgId(std::wstring &error) noexcept {
    const std::wstring key = g_classesSubKey + L"\\" + std::wstring(kOurProgId);
    const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    if (r == ERROR_FILE_NOT_FOUND) {
        return ERROR_SUCCESS;
    }
    if (r != ERROR_SUCCESS) {
        error = L"删除 ProgId 失败(error=" + std::to_wstring(r) + L")";
        return static_cast<DWORD>(r);
    }
    return ERROR_SUCCESS;
}

} // namespace DefaultApp