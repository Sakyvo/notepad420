// Tests for the replacer's default-open takeover layer (DefaultApp.cpp).
//
// The golden vectors are captured from the author's machine: 25 real
// UserChoice entries the Notepad3 replacer wrote, accepted by Windows.
// Reproducing them pins every byte of the hash port (md5 seeds, both mixing
// loops, xor-fold, base64). Any one-bit deviation fails loudly.
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../notepad420-replacer/DefaultApp.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond, what)                                                         \
	do {                                                                            \
		++g_checks;                                                                 \
		if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what)); } \
	} while (0)

// Task 021: 私有 ProgId 注册/回收的覆写存验(测试用记入 HKCU\Software\Classes\Applications\*,不构成受用户对系统改动了)。
static void TestOurProgIdRoundTrip() {
	std::wstring error;
	const std::wstring exe = L"C:\\unreal\\path\\notepad420.exe";
	DWORD rc = DefaultApp::WriteOurProgId(exe, error);
	CHECK(rc == ERROR_SUCCESS, "write ProgId succeeds");

	const std::wstring base = L"Software\\Classes\\Applications\\notepad420.exe";
	wchar_t buf[4096];
	DWORD sz = sizeof(buf), ty = 0;
	LONG r = RegGetValueW(HKEY_CURRENT_USER, (base + L"\\shell\\open\\command").c_str(),
						  nullptr, RRF_RT_ANY, &ty, buf, &sz);
	CHECK(r == ERROR_SUCCESS && wcsstr(buf, L"notepad420.exe") != nullptr,
		  "shell\\open\\command written");
	sz = sizeof(buf);
	r = RegGetValueW(HKEY_CURRENT_USER, (base + L"\\DefaultIcon").c_str(), nullptr,
					 RRF_RT_ANY, &ty, buf, &sz);
	CHECK(r == ERROR_SUCCESS && wcsstr(buf, L"notepad420.exe") != nullptr,
		  "DefaultIcon written");

	rc = DefaultApp::DeleteOurProgId(error);
	CHECK(rc == ERROR_SUCCESS, "delete ProgId succeeds");
	r = RegGetValueW(HKEY_CURRENT_USER, base.c_str(), nullptr, RRF_RT_ANY, nullptr, nullptr, 0);
	CHECK(r == ERROR_FILE_NOT_FOUND, "ProgId actually gone");

	// second delete = idempotent no-op
	rc = DefaultApp::DeleteOurProgId(error);
	CHECK(rc == ERROR_SUCCESS, "second delete is ok");
}



namespace {

const wchar_t kTestSid[] = L"S-1-5-21-2053099735-3675972999-4128786877-1001";
const wchar_t kNp3ProgId[] = L"Applications\\Notepad3_x64.exe";
// Live value of shell32.dll on the author's machine; also the hardcoded
// fallback on all Win10 builds.
const wchar_t kExperience[] =
	L"User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}";

struct GoldenVector {
	const wchar_t *ext;
	const wchar_t *hexDateTime;
	const wchar_t *hash;
};

// ext | hexdt | Hash — verbatim from the 25 NP3-written UserChoice entries.
const GoldenVector kGolden[] = {
	{L".cfg",          L"01dcf96311347000", L"DPjyvmDZyeI="},
	{L".conf",         L"01dcd47996c69400", L"DoJi+H0jMYM="},
	{L".config",       L"01dc5b88188f5a00", L"cV1pJd3pfes="},
	{L".dmp",          L"01dc05b1666c5c00", L"bYPIGC6XrzE="},
	{L".domains",      L"01dcfe089955c200", L"XHx1WzY4znU="},
	{L".editorconfig", L"01dd2d39b724a200", L"ZptNCDaHH+M="},
	{L".gitattributes",L"01dc93411e03e000", L"6LLYCuiLZ+8="},
	{L".gitconfig",    L"01dd287e6404a800", L"uY1KuNP4Cz8="},
	{L".gitignore",    L"01dcfc754a2b5800", L"rpGEoItOaaI="},
	{L".gitmodules",   L"01dce80f5d324000", L"ORY/asYcC5k="},
	{L".glsl",         L"01dd1e6affebe800", L"68baZZo0ioU="},
	{L".ini",          L"01dd02fbcd931c00", L"v+bvjg0YDCw="},
	{L".json",         L"01dcaeaa4e8fea00", L"Nbg5yh7JSMM="},
	{L".json5",        L"01dd472fb9f72600", L"x0LNpDAdUTk="},
	{L".jsonc",        L"01dd4339cba88a00", L"GG4L1UHC2QA="},
	{L".jsonl",        L"01dc7aca4c846600", L"gVkfHV7FYfU="},
	{L".log",          L"01dcf72c971aa600", L"ixytsNi5xLs="},
	{L".lua",          L"01dd2d4218e90a00", L"KSLCgS4SmT8="},
	{L".md",           L"01dd223cbfaab200", L"E7rg4v/PG2E="},
	{L".nip",          L"01dcfc773ed92c00", L"cgv6Zzximko="},
	{L".properties",   L"01dc51272c1dd800", L"4EdeAR2l8nQ="},
	{L".toml",         L"01dcfb19b85b0800", L"miWMFiRGqcI="},
	{L".txt",          L"01dcf6fd17c1ae00", L"CLjzhUFp5Ko="},
	{L".xml",          L"01dcd47996c69400", L"9fGABgbvq9Y="},
	{L".yaml",         L"01dd10f191303000", L"37I5qA7zLoU="},
};

void TestGoldenVectors() {
	for (const auto &v : kGolden) {
		const std::wstring got = DefaultApp::ComputeUserChoiceHash(
			v.ext, kTestSid, kNp3ProgId, v.hexDateTime, kExperience);
		if (got != v.hash) {
			++g_checks; ++g_failures;
			std::wprintf(L"FAIL golden %s: got %s want %s\n", v.ext, got.c_str(), v.hash);
		} else {
			++g_checks;
		}
	}
}

void TestIsNotepad3Target() {
	CHECK(DefaultApp::IsNotepad3Target(L"K:\\Notepad3\\Notepad3_x64.exe /z"),
		  "NP3 full path detected");
	CHECK(DefaultApp::IsNotepad3Target(L"applications\\notepad3_x64.exe"),
		  "NP3 progid lowercased detected");
	CHECK(DefaultApp::IsNotepad3Target(L"NOTEPAD3.EXE"),
		  "NP3 all-caps detected");
	CHECK(!DefaultApp::IsNotepad3Target(L"K:\\Notepad++\\notepad++.exe"),
		  "Notepad++ NOT touched");
	CHECK(!DefaultApp::IsNotepad3Target(L"Applications\\notepad.exe"),
		  "our own ProgId NOT touched");
	CHECK(!DefaultApp::IsNotepad3Target(L"Notepad2.exe"),
		  "Notepad2 NOT touched");
	CHECK(!DefaultApp::IsNotepad3Target(nullptr),
		  "nullptr safe");
}

void TestExperienceStringFallbackAgreement() {
	// On the author's machine the live-read string equals the hardcoded
	// fallback; if shell32.dll changes shape, fallback still applies.
	const std::wstring live = DefaultApp::ReadUserExperienceString();
	CHECK(!live.empty(), "experience string non-empty");
	CHECK(live.find(L"User Choice set via Windows User Experience") != std::wstring::npos,
		  "experience string has expected prefix");
	CHECK(live.back() == L'}', "experience string ends with '}'");
}

void TestSidAvailable() {
	const std::wstring sid = DefaultApp::GetCurrentUserSid();
	CHECK(!sid.empty(), "sid non-empty");
	CHECK(sid.rfind(L"S-1-", 0) == 0, "sid has SID prefix");
}

// End-to-end: write our UserChoice to a throwaway extension, then ask the
// same COM interface Explorer uses whether the hash is accepted.
void TestUserChoiceAcceptedBySystem() {
	const wchar_t *ext = L".np420test";
	const std::wstring ucKey =
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.np420test";

	// Clean slate.
	RegDeleteKeyW(HKEY_CURRENT_USER, (ucKey + L"\\UserChoice").c_str());
	RegDeleteKeyW(HKEY_CURRENT_USER, ucKey.c_str());

	// kOurProgId 必须先注册(生产路径的替换流程同样先写 ProgId,再改 UserChoice)
	std::wstring progErr;
	if (DefaultApp::WriteOurProgId(L"C:\\unreal\\path\\notepad420.exe", progErr) != ERROR_SUCCESS) {
		CHECK(false, "register ProgId first");
		return;
	}

	std::wstring error;
	if (!DefaultApp::WriteUserChoice(ext, DefaultApp::kOurProgId, error)) {
		CHECK(false, "WriteUserChoice failed");
		std::wprintf(L"  %s\n", error.c_str());
		return;
	}
	CHECK(true, "WriteUserChoice succeeded");

	// Read back the registry + call QueryCurrentDefault.
	LPWSTR result = nullptr;
	bool accepted = false;
	if (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) >= 0) {
		IApplicationAssociationRegistration *aar = nullptr;
		HRESULT hr = CoCreateInstance(
			CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC_SERVER,
			IID_IApplicationAssociationRegistration, (void **)&aar);
		if (hr == S_OK && aar) {
			const HRESULT q = aar->QueryCurrentDefault(ext, AT_FILEEXTENSION, AL_EFFECTIVE, &result);
			accepted = (q == S_OK) && (result != nullptr);
			if (accepted) {
				CoTaskMemFree(result);
			}
			aar->Release();
		}
		CoUninitialize();
	}
	CHECK(accepted, "QueryCurrentDefault accepted our hash");

	// Negative control: corrupt the hash, verify the system rejects it. This
	// proves the positive check above isn't vacuous.
	{
		HKEY h;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, (ucKey + L"\\UserChoice").c_str(),
							0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
			const wchar_t garbage[] = L"AAAAAAAABAd=";
			RegSetValueExW(h, L"Hash", 0, REG_SZ, (const BYTE *)garbage,
						   (DWORD)(wcslen(garbage) + 1) * 2);
			RegCloseKey(h);
			if (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) >= 0) {
				IApplicationAssociationRegistration *aar = nullptr;
				if (CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr,
									 CLSCTX_INPROC_SERVER,
									 IID_IApplicationAssociationRegistration,
									 (void **)&aar) == S_OK) {
					LPWSTR out = nullptr;
					const HRESULT q = aar->QueryCurrentDefault(ext, AT_FILEEXTENSION,
															   AL_EFFECTIVE, &out);
					CHECK(q != S_OK || out == nullptr,
						  "corrupted hash is rejected by the system");
					if (out) { CoTaskMemFree(out); }
					aar->Release();
				}
				CoUninitialize();
			}
		}
	}

	// Cleanup.
	RegDeleteKeyW(HKEY_CURRENT_USER, (ucKey + L"\\UserChoice").c_str());
	RegDeleteKeyW(HKEY_CURRENT_USER, ucKey.c_str());
	SHChangeNotify(0x08000000, 0, nullptr, nullptr);
}

} // namespace

int main() {
	TestGoldenVectors();
	TestIsNotepad3Target();
	TestExperienceStringFallbackAgreement();
	TestSidAvailable();
	TestUserChoiceAcceptedBySystem();
	TestOurProgIdRoundTrip();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures;
}
