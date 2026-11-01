// SHA-256 (FIPS 180-4), used only to check the manifest's corpus pin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace conduit {

std::string sha256_hex(const void* data, size_t len);

}  // namespace conduit
