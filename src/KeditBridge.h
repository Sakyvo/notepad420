/******************************************************************************
*
* notepad420
*
* KeditBridge.h
*   kedit bridge-paste 档的一致性接缝: JSON 载荷解析、字节吞吐、
*   markdown 引用 URI 原位替换。Win32-free 便于 headless 测试。
*
* 语义(与 kedit clipboardCopy.js 对齐):
*   payload = { v:1, text:"<同 text/plain 字节一致>", images:[{uri,mime,dataBase64}] }
*   每一张 images[i] 对应 text 中第 i 个本地 /imgs/... 引用(出现序,含重复)。
*
******************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KeditBridge {

/// 图像字节 -> 扩展名(取 magic,未识别返回 nullptr)
inline const char *ExtensionForMagic(const uint8_t *bytes, std::size_t len) noexcept {
	auto head = [&](const char *sig, std::size_t n) {
		return len >= n && std::memcmp(bytes, sig, n) == 0;
	};
	if (head("\x89PNG\r\n\x1a\n", 8)) return ".png";
	if (head("\xFF\xD8\xFF", 3)) return ".jpg";
	if (head("GIF8", 4)) return ".gif";
	if (len >= 12 && head("RIFF", 4) && std::memcmp(bytes + 8, "WEBP", 4) == 0) return ".webp";
	if (head("BM", 2)) return ".bmp";
	if (len >= 12 && std::memcmp(bytes + 4, "ftyp", 4) == 0) return ".avif";	// 粗匹配:ftyp 盒
	return nullptr;
}

/// mime -> 扩展名(kedit best-effort 声明;未知返回 nullptr)。
inline const char *ExtensionForMime(const std::string &mime) noexcept {
	struct { const char *mime; const char *ext; } kTypes[] = {
		{ "image/png", ".png" },
		{ "image/jpeg", ".jpg" },
		{ "image/gif", ".gif" },
		{ "image/webp", ".webp" },
		{ "image/bmp", ".bmp" },
		{ "image/avif", ".avif" },
		{ "image/svg+xml", ".svg" },
	};
	for (const auto &e : kTypes) {
		if (mime == e.mime) return e.ext;
	}
	return nullptr;
}

/// 极薄的 JSON "string→string" map 解析器，供 Web Custom Format Map 使用。
/// 只接收平面 {"k":"v",...}。返回 false 当解析失败或 map 中不含目标键。
/// 与 Newtonsoft 式协议兼容只限 ASCII key； value 支json 字符串(ting含转义）。
bool ParseStringMap(const char *json, std::size_t length,
					const std::string &wantedKey, std::string &outValue) noexcept;

/// 根据 Mime essence(本 fork 中固定为 "application/x-notepad420-paste")从 map
/// JSON 里查槽位名(如 "Web Custom Format0")。查不到返回空串。
inline std::string FindSlotName(const std::string &mapJson) noexcept {
	std::string out;
	return ParseStringMap(mapJson.data(), mapJson.size(),
						  "application/x-notepad420-paste", out) ? out : std::string{};
}

struct BridgeImage {
	std::string uri;
	std::string mime;
	std::string dataBase64;
};

struct BridgePayload {
	std::string text;
	std::vector<BridgeImage> images;
};

/// 极简 JSON 解码器——只支持我们的载荷形态。失败返回 false。
bool ParseBridgePayload(const char *json, std::size_t length, BridgePayload &out) noexcept;

/// 文本 + bridges -> 替换后的文本。
/// 须调用侧先保证: baseText 与 payload.text 在换行规范化（CRLF→LF）后严格一致。
/// 这是为了让平台端把 text/plain LF 规范为 CRLF 时(Chromium 在 Windows 会这么做)
/// 仍可对齐: 我们比对 normalize(payload.text) == normalize(clipboardText),
/// 但重写/插入的是原始剪贴板文本（避免把浏览器生成的平台端格式付纣复麻给文档）。
/// 每个 images[i] 的 base64 先落盘得到路径,然后把 baseText 中该图第一个
/// 还未处理的 `![...](<uri>)` 的 uri 原位改写为 path。
/// sink: (bytes,len,ext, outPath) -> bool 失败即整档放弃(返回 false)。
using KeditSink = bool (*)(void *context, const void *bytes, std::size_t length,
						   const char *extension, std::string &outPath);

bool RewriteBridgeText(const BridgePayload &payload,
					   const std::string &baseText,
					   void *sinkContext, KeditSink sink,
					   std::string &out) noexcept;

/// 换行规范性化: 把 \r\n 与 acidware 的 \r 都应用为 \n，返回新串。
std::string NormalizeEolLf(const std::string &in) noexcept;

bool RewriteBridgeText(const BridgePayload &payload,
					   void *sinkContext, KeditSink sink,
					   std::string &out) noexcept;

} // namespace KeditBridge
