/******************************************************************************
*
* notepad420
*
* Base64.cpp
*   See Base64.h.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#include "Base64.h"

// The alphabet and decoding table match upstream Notepad4's Helpers.cpp; they
// moved here so the codec has a single, unit-tested implementation.

size_t Base64Encode(char *output, const uint8_t *src, size_t length, bool urlSafe) noexcept {
	char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (urlSafe) {
		table[62] = '-';
		table[63] = '_';
	}

	char *p = output;
	size_t i = 0;
	while (i + 3 <= length) {
		i += 3;
		const uint8_t C0 = *src++;
		const uint8_t C1 = *src++;
		const uint8_t C2 = *src++;
		*p++ = table[(C0 >> 2)];
		*p++ = table[((C0 & 3) << 4) | (C1 >> 4)];
		*p++ = table[((C1 & 15) << 2) | (C2 >> 6)];
		*p++ = table[C2 & 0x3f];
	}
	if (i < length) {
		i++;
		const uint8_t C0 = src[0];
		const uint8_t C1 = (i < length) ? src[1] : 0;
		*p++ = table[(C0 >> 2)];
		*p++ = table[((C0 & 3) << 4) | (C1 >> 4)];
		*p++ = (i < length) ? table[((C1 & 15) << 2)] : '=';
		*p++ = '=';
	}
	return p - output;
}

// see GenerateBase64Table() in tools/GenerateTable.py
static const uint8_t Base64DecodingTable[128] = {
128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  62, 128,  62, 128,  63,
 52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 128, 128, 128, 128, 128, 128,
128,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
 15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25, 128, 128, 128, 128,  63,
128,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
 41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51, 128, 128, 128, 128, 128,
};

size_t Base64Decode(uint8_t *output, const uint8_t *src, size_t length) noexcept {
	uint32_t value = 0;
	uint8_t *p = output;
	size_t i = 0;
	while(i < length) {
		uint8_t ch = *src;
		if (static_cast<signed char>(ch) < 0) {
			break;
		}
		ch = Base64DecodingTable[ch];
		if (static_cast<signed char>(ch) < 0) {
			break;
		}
		value = (value << 6) | ch;
		++src;
		i++;
		if ((i & 3) == 0) {
			*p++ = static_cast<uint8_t>(value >> 16);
			*p++ = static_cast<uint8_t>(value >> 8);
			*p++ = static_cast<uint8_t>(value);
			value = 0;
		}
	}
	i &= 3;
	if (i != 0) {
		if (i == 3) {
			value >>= (8 - 6);
			*p++ = static_cast<uint8_t>(value >> 8);
			*p++ = static_cast<uint8_t>(value);
		} else {
			*p++ = static_cast<uint8_t>(value >> (16 - 12));
		}
	}
	return p - output;
}
