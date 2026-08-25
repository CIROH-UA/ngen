/*
Author: Nels Frazier
Copyright (C) 2025 Lynker
------------------------------------------------------------------------
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
------------------------------------------------------------------------
INTERNAL — implementation detail of wire_format::RecordPrefix.

This header provides the little-endian byte-level encode/decode
primitives that `wire_format::encode_record_prefix` and
`decode_record_prefix` use to encode the prefix's integer fields. It
is not a general-purpose serialization utility, has no API contract
of its own, and must not be used outside `wire_format.hpp`. The
design rationale for field-level LE I/O (why not boost archives,
the cross-host portability claims the wire format makes, the
two's-complement assumption and its run-host canary tests, etc.)
lives with the wire format itself — see `wire_format.hpp`.

The encode_/decode_ primitives operate on raw `uint8_t*` buffers and
are the source of truth for LE byte semantics.

The write_/read_ primitives are thin stream adapters:
encode into a small stack buffer then hand it to
`ostream::write`, or `istream::read` into a buffer then decode. This
allows callers to use stream objects OR to create equivalent buffers directly
without losing the LE byte semantics.

Visibility is enforced by include-path discipline: this header is
only on the PRIVATE include path of the `ngen_bmi_protocols`
library's own translation units and of the `test_bmi_protocols`
test target. It is never exported via `target_include_directories(PUBLIC ...)`.
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <type_traits>

namespace models {
namespace bmi {
namespace protocols {
namespace serialization {
namespace byte_io {

// Compile-time guard for the `reinterpret_cast<char*>(uint8_t*)`
// bridge in the stream adapters below. The standard does not
// mandate that `std::uint8_t` alias `unsigned char` — it only
// requires that the type exist, be unsigned, and be exactly 8
// bits. In most scenarios the two are aliases, and the
// reinterpret_cast is well-defined via the standard's
// `char`/`unsigned char` aliasing rules. The assertion below
// catches any future target where the alias does not hold.
static_assert(
    std::is_same<std::uint8_t, unsigned char>::value,
    "byte_io: std::uint8_t must alias unsigned char on this "
    "target — the stream adapters' reinterpret_cast<char*> "
    "bridge depends on it. See the comment immediately above "
    "this assertion for the rationale."
);

// Little-endian extraction / reassembly mechanics:
//
// (v & 0xFF) keeps the lowest 8 bits — the byte that goes first on
// disk. A right shift then brings the next byte down into the lowest
// position; repeat for each additional byte the type holds,
// incrementing the shift by 8 each time. Reading inverts this: the
// lowest byte on disk goes into the low-byte position of the result
// (no shift), and each subsequent byte is widened to the target
// unsigned type and shifted left by 8 more than the previous, then
// OR'd together.
//
// The 0xFF mask in the encode path is redundant with the
// `static_cast<uint8_t>` that follows (the cast already truncates to
// 8 bits) but is kept as an explicit statement of intent on the
// line. The widening cast in the decode path is required: a raw
// `uint8_t << 8` auto-promotes to int, losing the
// explicit-target-type semantics.
//
// Signed types travel through their unsigned bit-pattern via
// `std::memcpy`. This assumes two's-complement signed representation
// — see wire_format.hpp for the assumption's role in the wire
// format's portability claim and for the run-host canary tests that
// verify it.

// ============================================================
// Buffer encode/decode primitives — source of truth.
// ============================================================

inline void encode_u8(uint8_t* p, uint8_t v) {
    p[0] = v;
}

inline uint8_t decode_u8(const uint8_t* p) {
    return p[0];
}

inline void encode_u16_le(uint8_t* p, uint16_t v) {
    // clang-format off
    p[0] = static_cast<uint8_t>( v       & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    // clang-format on
}

inline uint16_t decode_u16_le(const uint8_t* p) {
    // The outer static_cast<uint16_t> is the demote-back step that
    // decode_u32_le and decode_u64_le don't need. `uint16_t << 8`
    // promotes to int because uint16_t is smaller than int, so the
    // shift result is int-typed; the cast narrows it back and
    // suppresses any -Wconversion / -Wnarrowing warning at the OR.
    // clang-format off
    return static_cast<uint16_t>(p[0])
         | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
    // clang-format on
}

inline void encode_u32_le(uint8_t* p, uint32_t v) {
    // clang-format off
    p[0] = static_cast<uint8_t>( v        & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8)  & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    // clang-format on
}

inline uint32_t decode_u32_le(const uint8_t* p) {
    // clang-format off
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
    // clang-format on
}

inline void encode_u64_le(uint8_t* p, uint64_t v) {
    // clang-format off
    p[0] = static_cast<uint8_t>( v        & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8)  & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[4] = static_cast<uint8_t>((v >> 32) & 0xFF);
    p[5] = static_cast<uint8_t>((v >> 40) & 0xFF);
    p[6] = static_cast<uint8_t>((v >> 48) & 0xFF);
    p[7] = static_cast<uint8_t>((v >> 56) & 0xFF);
    // clang-format on
}

inline uint64_t decode_u64_le(const uint8_t* p) {
    // clang-format off
    return  static_cast<uint64_t>(p[0])
         | (static_cast<uint64_t>(p[1]) << 8)
         | (static_cast<uint64_t>(p[2]) << 16)
         | (static_cast<uint64_t>(p[3]) << 24)
         | (static_cast<uint64_t>(p[4]) << 32)
         | (static_cast<uint64_t>(p[5]) << 40)
         | (static_cast<uint64_t>(p[6]) << 48)
         | (static_cast<uint64_t>(p[7]) << 56);
    // clang-format on
}

inline void encode_i32_le(uint8_t* p, int32_t v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    encode_u32_le(p, u);
}

inline int32_t decode_i32_le(const uint8_t* p) {
    uint32_t u = decode_u32_le(p);
    int32_t v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

inline void encode_i64_le(uint8_t* p, int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    encode_u64_le(p, u);
}

inline int64_t decode_i64_le(const uint8_t* p) {
    uint64_t u = decode_u64_le(p);
    int64_t v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

// ============================================================
// Stream adapters — thin wrappers around the buffer primitives.
//
// Each adapter contains a `reinterpret_cast<char*>(uint8_t*)`
// that bridges the encode/decode primitives' byte-buffer type
// to the iostream API. The cast is guarded by the static_assert
// at the top of this header.
// ============================================================

inline void write_u8(std::ostream& out, uint8_t v) {
    uint8_t buf[1];
    encode_u8(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 1);
}

inline bool read_u8(std::istream& in, uint8_t& v) {
    uint8_t buf[1];
    if (!in.read(reinterpret_cast<char*>(buf), 1)) return false;
    if (in.gcount() != 1) return false;
    v = decode_u8(buf);
    return true;
}

inline void write_u16_le(std::ostream& out, uint16_t v) {
    uint8_t buf[2];
    encode_u16_le(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 2);
}

inline bool read_u16_le(std::istream& in, uint16_t& v) {
    uint8_t buf[2];
    if (!in.read(reinterpret_cast<char*>(buf), 2)) return false;
    if (in.gcount() != 2) return false;
    v = decode_u16_le(buf);
    return true;
}

inline void write_u32_le(std::ostream& out, uint32_t v) {
    uint8_t buf[4];
    encode_u32_le(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

inline bool read_u32_le(std::istream& in, uint32_t& v) {
    uint8_t buf[4];
    if (!in.read(reinterpret_cast<char*>(buf), 4)) return false;
    if (in.gcount() != 4) return false;
    v = decode_u32_le(buf);
    return true;
}

inline void write_u64_le(std::ostream& out, uint64_t v) {
    uint8_t buf[8];
    encode_u64_le(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 8);
}

inline bool read_u64_le(std::istream& in, uint64_t& v) {
    uint8_t buf[8];
    if (!in.read(reinterpret_cast<char*>(buf), 8)) return false;
    if (in.gcount() != 8) return false;
    v = decode_u64_le(buf);
    return true;
}

inline void write_i32_le(std::ostream& out, int32_t v) {
    uint8_t buf[4];
    encode_i32_le(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

inline bool read_i32_le(std::istream& in, int32_t& v) {
    uint8_t buf[4];
    if (!in.read(reinterpret_cast<char*>(buf), 4)) return false;
    if (in.gcount() != 4) return false;
    v = decode_i32_le(buf);
    return true;
}

inline void write_i64_le(std::ostream& out, int64_t v) {
    uint8_t buf[8];
    encode_i64_le(buf, v);
    out.write(reinterpret_cast<const char*>(buf), 8);
}

inline bool read_i64_le(std::istream& in, int64_t& v) {
    uint8_t buf[8];
    if (!in.read(reinterpret_cast<char*>(buf), 8)) return false;
    if (in.gcount() != 8) return false;
    v = decode_i64_le(buf);
    return true;
}

} // namespace byte_io
} // namespace serialization
} // namespace protocols
} // namespace bmi
} // namespace models
