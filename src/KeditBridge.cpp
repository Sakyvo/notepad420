/******************************************************************************
*
* notepad420
*
* KeditBridge.cpp
*   kedit bridge-paste 档的 JSON 解码与文本重写实现(纯代码,无外赖)。
*
******************************************************************************/
#include <windows.h>
#include <cstring>
#include <cwctype>
#include "../src/KeditBridge.h"
#include "../src/Base64.h"

namespace {

class JsonCursor {
public:
	JsonCursor(const char *data, std::size_t len) : p_(data), end_(data + len) {}

	bool Ok() const noexcept { return p_ != nullptr; }
	void Fail() noexcept { p_ = nullptr; }

	void SkipWs() noexcept {
		if (!Ok()) return;
		while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) {
			++p_;
		}
	}

	bool Consume(char c) noexcept {
		if (!Ok()) return false;
		SkipWs();
		if (p_ == end_ || *p_ != c) return false;	// 不破坏游标,调用方可重试别的分隔符
		++p_;
		return true;
	}

	bool ConsumeLiteral(const char *lit) noexcept {
		if (!Ok()) return false;
		SkipWs();
		const std::size_t n = std::strlen(lit);
		if (static_cast<std::size_t>(end_ - p_) < n || std::memcmp(p_, lit, n) != 0) {
			Fail(); return false;
		}
		p_ += n;
		return true;
	}

	bool ParseString(std::string &out) noexcept {
		out.clear();
		if (!Ok()) return false;
		SkipWs();
		if (p_ == end_ || *p_ != '"') { Fail(); return false; }
		++p_;
		while (p_ < end_) {
			const char c = *p_;
			if (c == '"') { ++p_; return true; }
			if (c == '\\') {
				++p_;
				if (p_ >= end_) { Fail(); return false; }
				const char esc = *p_++;
				switch (esc) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					if (end_ - p_ < 4) { Fail(); return false; }
					unsigned cp = 0;
					for (int i = 0; i < 4; ++i) {
						const char h = *p_++;
						cp <<= 4;
						if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
						else if (h >= 'a' && h <= 'f') cp |= 10u + (unsigned)(h - 'a');
						else if (h >= 'A' && h <= 'F') cp |= 10u + (unsigned)(h - 'A');
						else { Fail(); return false; }
					}
					// surrogate pair
					if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6
							&& p_[0] == '\\' && p_[1] == 'u') {
						p_ += 2;
						unsigned low = 0;
						for (int i = 0; i < 4; ++i) {
							const char h = *p_++;
							low <<= 4;
							if (h >= '0' && h <= '9') low |= (unsigned)(h - '0');
							else if (h >= 'a' && h <= 'f') low |= 10u + (unsigned)(h - 'a');
							else if (h >= 'A' && h <= 'F') low |= 10u + (unsigned)(h - 'A');
							else { Fail(); return false; }
						}
						if (low < 0xDC00 || low > 0xDFFF) { Fail(); return false; }
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					}
					// to_utf8
					if (cp < 0x80) out += static_cast<char>(cp);
					else if (cp < 0x800) {
						out += static_cast<char>(0xC0 | (cp >> 6));
						out += static_cast<char>(0x80 | (cp & 0x3F));
					} else if (cp < 0x10000) {
						out += static_cast<char>(0xE0 | (cp >> 12));
						out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (cp & 0x3F));
					} else {
						out += static_cast<char>(0xF0 | (cp >> 18));
						out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
						out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (cp & 0x3F));
					}
					break;
				}
				default: Fail(); return false;
				}
				continue;
			}
			if (static_cast<unsigned char>(c) < 0x20) { Fail(); return false; }
			out += c; ++p_;
		}
		Fail();
		return false;
	}

	bool ParseNumber(std::string &out) noexcept {
		out.clear();
		if (!Ok()) return false;
		SkipWs();
		if (p_ == end_) { Fail(); return false; }
		if (*p_ == '-') { out += *p_++; }
		while (p_ < end_ && (isdigit(static_cast<unsigned char>(*p_)) ||
							 *p_ == '.' || *p_ == 'e' || *p_ == 'E' ||
							 *p_ == '+' || *p_ == '-')) {
			out += *p_++;
		}
		if (out.empty()) { Fail(); return false; }
		return true;
	}

	const char *p_;
	const char *end_;
};

/// 一个 `{ uri, mime, dataBase64 }` 对象
bool ParseImage(JsonCursor &c, KeditBridge::BridgeImage &img) {
	if (!c.Consume('{')) return false;
	bool gotUri = false, gotData = false;
	for (;;) {
		std::string key;
		if (!c.ParseString(key)) return false;
		if (!c.Consume(':')) return false;
		if (key == "uri") {
			if (!c.ParseString(img.uri)) return false; gotUri = true;
		} else if (key == "mime") {
			if (!c.ParseString(img.mime)) return false;
		} else if (key == "dataBase64") {
			if (!c.ParseString(img.dataBase64)) return false; gotData = true;
		} else {
			return false;	// 未知字段视为格式不符
		}
		if (c.Consume(',')) continue;
		if (c.Consume('}')) break;
		return false;
	}
	return gotUri && gotData;
}

bool ParseImagesArray(JsonCursor &c, std::vector<KeditBridge::BridgeImage> &out) {
	if (!c.Consume('[')) return false;
	c.SkipWs();
	// peek ']' (empty array)
	const char *peek = c.p_;
	while (peek < c.end_ && (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')) ++peek;
	if (peek != c.end_ && *peek == ']') { c.p_ = peek + 1; return true; }
	for (;;) {
		KeditBridge::BridgeImage img;
		if (!ParseImage(c, img)) return false;
		out.push_back(std::move(img));
		if (c.Consume(',')) continue;
		if (c.Consume(']')) return true;
		return false;
	}
}

} // namespace

namespace KeditBridge {

bool ParseStringMap(const char *json, std::size_t length,
					const std::string &wantedKey, std::string &outValue) noexcept {
	outValue.clear();
	if (json == nullptr || length == 0) return false;
	JsonCursor c(json, length);
	if (!c.Consume('{')) return false;
	c.SkipWs();
	const char *peek = c.p_;
	while (peek < c.end_ && (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')) ++peek;
	if (peek != c.end_ && *peek == '}') {
		c.p_ = peek + 1;
		c.SkipWs();
		return false;   // 空 map 不含 wantedKey
	}
	bool found = false;
	for (;;) {
		std::string key;
		if (!c.ParseString(key)) return false;
		if (!c.Consume(':')) return false;
		std::string value;
		if (!c.ParseString(value)) return false;
		if (key == wantedKey) {
			outValue = value;
			found = true;
		}
		if (c.Consume(',')) continue;
		if (c.Consume('}')) break;
		return false;
	}
	c.SkipWs();
	return c.Ok() && c.p_ == c.end_ && found;
}


bool ParseBridgePayload(const char *json, std::size_t length, BridgePayload &out) noexcept {
	out = BridgePayload{};
	if (json == nullptr || length == 0) return false;
	JsonCursor c(json, length);
	if (!c.Consume('{')) return false;
	bool gotV = false, gotText = false, gotImages = false;
	for (;;) {
		std::string key;
		if (!c.ParseString(key)) return false;
		if (!c.Consume(':')) return false;
		if (key == "v") {
			std::string num;
			if (!c.ParseNumber(num)) return false;
			if (num != "1") return false;
			gotV = true;
		} else if (key == "text") {
			if (!c.ParseString(out.text)) return false;
			gotText = true;
		} else if (key == "images") {
			if (!ParseImagesArray(c, out.images)) return false;
			gotImages = true;
		} else {
			return false;
		}
		if (c.Consume(',')) continue;
		if (c.Consume('}')) break;
		return false;
	}
	c.SkipWs();
	if (!c.Ok() || c.p_ != c.end_) return false;
	return gotV && gotText && gotImages;
}

bool RewriteBridgeText(const BridgePayload &payload,
					   const std::string &baseText,
					   void *sinkContext, KeditSink sink,
					   std::string &out) noexcept {
	out.clear();
	if (sink == nullptr) return false;

	std::string cur = baseText;
	if (payload.images.empty()) return false;	// 空 bridge 不应该走这档

	// Phase 1: locate every `![alt](uri)` span (uri position is where the payload
	// recorded). All spans must exist or we静退—— no image written yet.
	struct Span { std::size_t start, end; };
	std::vector<Span> spans;
	spans.reserve(payload.images.size());
	std::size_t cursor = 0;
	for (std::size_t i = 0; i < payload.images.size(); ++i) {
		const std::string &needle = payload.images[i].uri;
		if (needle.empty()) return false;
		Span span{std::string::npos, std::string::npos};
		// Find this uri occurrence (may repeat); wrap must be `![<alt>](  uri  )`.
		for (std::size_t probe = cur.find(needle, cursor); probe != std::string::npos;
				 probe = cur.find(needle, probe + 1)) {
			// Walk back/forward over whitespace inside parens.
			std::size_t l = probe;
			while (l > 0 && (cur[l - 1] == ' ' || cur[l - 1] == '	')) --l;
			std::size_t r = probe + needle.size();
			while (r < cur.size() && (cur[r] == ' ' || cur[r] == '	')) ++r;
			if (l == 0 || cur[l - 1] != '(' || r >= cur.size() || cur[r] != ')') continue;
			// Expect `](uri)` → walk back for `![`...`]`.
			if (l < 2 || cur[l - 2] != ']') continue;
			const std::size_t altOpen = cur.rfind("![", l - 2);
			if (altOpen == std::string::npos) continue;
			span = Span{ altOpen, r + 1 };
			break;
		}
		if (span.start == std::string::npos) return false;
		spans.push_back(span);
		cursor = span.end;
	}

	// Phase 2: write every image via sink (failures靜退 before any replace).
	std::vector<std::string> paths;
	paths.reserve(payload.images.size());
	for (const auto &img : payload.images) {
		std::string compact;
		compact.reserve(img.dataBase64.size());
		for (char c : img.dataBase64) {
			if (c != ' ' && c != '\t' && c != '\r' && c != '\n') compact += c;
		}
		if (compact.empty() || (compact.size() & 3u) != 0) return false;
		std::vector<uint8_t> decoded((compact.size() / 4) * 3);
		const std::size_t decodedSize = Base64Decode(decoded.data(),
				reinterpret_cast<const uint8_t *>(compact.data()), compact.size());
		if (decodedSize == 0) return false;

		const char *ext = ExtensionForMagic(decoded.data(), decodedSize);
		if (ext == nullptr) ext = ExtensionForMime(img.mime);
		if (ext == nullptr) ext = ".png";

		std::string path;
		if (!sink(sinkContext, decoded.data(), decodedSize, ext, path)) {
			return false;
		}
		paths.push_back(std::move(path));
	}

	// Phase 3: back-to-front replace each `![alt](uri)` span with its bare path.
	for (std::size_t i = spans.size(); i > 0; --i) {
		const auto &span = spans[i - 1];
		cur.replace(span.start, span.end - span.start, paths[i - 1]);
	}
	out = std::move(cur);
	return true;
}

bool RewriteBridgeText(const BridgePayload &payload,
					   void *sinkContext, KeditSink sink,
					   std::string &out) noexcept {
	return RewriteBridgeText(payload, payload.text, sinkContext, sink, out);
}

std::string NormalizeEolLf(const std::string &in) noexcept {
	std::string out;
	out.reserve(in.size());
	for (std::size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '\r') {
			if (i + 1 < in.size() && in[i + 1] == '\n') ++i;	// \r\n → \n
			// \r 单独出现也统一为 \n
		}
		if (i < in.size()) {
			out.push_back(in[i] == '\r' ? '\n' : in[i]);
		}
	}
	return out;
}

} // namespace KeditBridge

