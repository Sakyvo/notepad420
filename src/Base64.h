/******************************************************************************
*
* notepad420
*
* Base64.h
*   Base64 encode/decode. Dependency-free (no Windows headers), so the codec
*   can be unit tested on its own — see tools/tests/base64_test.cpp.
*
* See License.txt for details about distribution and modification.
*
******************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>

/// Encodes `length` bytes into `output`, NUL-terminating nothing and returning
/// the number of characters written. `urlSafe` swaps '+' and '/' for '-' and '_'.
size_t Base64Encode(char *output, const uint8_t *src, size_t length, bool urlSafe) noexcept;

/// Decodes base64 into `output`, stopping at the first character outside the
/// alphabet, and returning the number of bytes written.
size_t Base64Decode(uint8_t *output, const uint8_t *src, size_t length) noexcept;