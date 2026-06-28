#include "core/history/ZipReader.hpp"

#include <algorithm>
#include <limits>
#include <optional>

#include <zlib.h>

namespace hftrec::history {

namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t pos) noexcept {
    return static_cast<std::uint16_t>(data[pos] | (static_cast<std::uint16_t>(data[pos + 1u]) << 8u));
}

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t pos) noexcept {
    return static_cast<std::uint32_t>(data[pos]) |
           (static_cast<std::uint32_t>(data[pos + 1u]) << 8u) |
           (static_cast<std::uint32_t>(data[pos + 2u]) << 16u) |
           (static_cast<std::uint32_t>(data[pos + 3u]) << 24u);
}

bool hasBytes(const std::vector<std::uint8_t>& data, std::size_t pos, std::size_t len) noexcept {
    return pos <= data.size() && len <= data.size() - pos;
}

bool inflateRaw(const std::uint8_t* input,
                std::size_t inputSize,
                std::size_t outputSize,
                std::string& out,
                std::string& error) {
    out.assign(outputSize, '\0');
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    stream.avail_in = static_cast<uInt>(inputSize);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = "zlib inflateInit2 failed";
        return false;
    }
    const int rc = inflate(&stream, Z_FINISH);
    const int endRc = inflateEnd(&stream);
    if (rc != Z_STREAM_END || endRc != Z_OK || stream.total_out != outputSize) {
        error = "zlib failed to inflate ZIP member";
        return false;
    }
    return true;
}

}  // namespace

bool extractSingleFileZip(const std::vector<std::uint8_t>& zip,
                          std::string& fileName,
                          std::string& text,
                          std::string& error) {
    fileName.clear();
    text.clear();
    error.clear();
    if (zip.size() < 22u) {
        error = "ZIP file is too small";
        return false;
    }

    std::size_t eocd = std::string::npos;
    const std::size_t minPos = zip.size() > 65557u ? zip.size() - 65557u : 0u;
    for (std::size_t pos = zip.size() - 22u;; --pos) {
        if (u32(zip, pos) == 0x06054b50u) {
            eocd = pos;
            break;
        }
        if (pos == minPos) break;
    }
    if (eocd == std::string::npos || !hasBytes(zip, eocd, 22u)) {
        error = "ZIP end-of-central-directory record not found";
        return false;
    }
    const std::uint16_t entries = u16(zip, eocd + 10u);
    const std::uint32_t centralSize = u32(zip, eocd + 12u);
    const std::uint32_t centralOffset = u32(zip, eocd + 16u);
    if (entries == 0u || !hasBytes(zip, centralOffset, centralSize)) {
        error = "ZIP central directory is invalid";
        return false;
    }

    std::optional<std::size_t> selectedCentral{};
    std::size_t pos = centralOffset;
    for (std::uint16_t i = 0; i < entries; ++i) {
        if (!hasBytes(zip, pos, 46u) || u32(zip, pos) != 0x02014b50u) {
            error = "ZIP central directory entry is corrupt";
            return false;
        }
        const std::uint16_t nameLen = u16(zip, pos + 28u);
        const std::uint16_t extraLen = u16(zip, pos + 30u);
        const std::uint16_t commentLen = u16(zip, pos + 32u);
        if (!hasBytes(zip, pos + 46u, static_cast<std::size_t>(nameLen) + extraLen + commentLen)) {
            error = "ZIP central directory entry exceeds file size";
            return false;
        }
        const std::string name(reinterpret_cast<const char*>(zip.data() + pos + 46u), nameLen);
        if (!name.empty() && name.back() != '/') {
            if (selectedCentral.has_value()) {
                error = "ZIP archive contains more than one file";
                return false;
            }
            selectedCentral = pos;
        }
        pos += 46u + nameLen + extraLen + commentLen;
    }
    if (!selectedCentral.has_value()) {
        error = "ZIP archive does not contain a file";
        return false;
    }

    const std::size_t central = *selectedCentral;
    const std::uint16_t method = u16(zip, central + 10u);
    const std::uint32_t expectedCrc = u32(zip, central + 16u);
    const std::uint32_t compressedSize = u32(zip, central + 20u);
    const std::uint32_t uncompressedSize = u32(zip, central + 24u);
    const std::uint16_t nameLen = u16(zip, central + 28u);
    const std::uint32_t localOffset = u32(zip, central + 42u);
    fileName.assign(reinterpret_cast<const char*>(zip.data() + central + 46u), nameLen);

    if (!hasBytes(zip, localOffset, 30u) || u32(zip, localOffset) != 0x04034b50u) {
        error = "ZIP local file header is invalid";
        return false;
    }
    const std::uint16_t localNameLen = u16(zip, localOffset + 26u);
    const std::uint16_t localExtraLen = u16(zip, localOffset + 28u);
    const std::size_t dataOffset = localOffset + 30u + localNameLen + localExtraLen;
    if (!hasBytes(zip, dataOffset, compressedSize)) {
        error = "ZIP compressed member exceeds file size";
        return false;
    }
    if (uncompressedSize > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        error = "ZIP member is too large for importer v1";
        return false;
    }

    if (method == 0u) {
        text.assign(reinterpret_cast<const char*>(zip.data() + dataOffset), compressedSize);
    } else if (method == 8u) {
        if (!inflateRaw(zip.data() + dataOffset, compressedSize, uncompressedSize, text, error)) return false;
    } else {
        error = "ZIP compression method is unsupported";
        return false;
    }
    const std::uint32_t observedCrc = crc32(crc32(0L, Z_NULL, 0),
                                           reinterpret_cast<const Bytef*>(text.data()),
                                           static_cast<uInt>(text.size()));
    if (observedCrc != expectedCrc) {
        error = "ZIP member CRC mismatch";
        return false;
    }
    return true;
}

}  // namespace hftrec::history
