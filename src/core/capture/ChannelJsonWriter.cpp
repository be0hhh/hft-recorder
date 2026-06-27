#include "core/capture/ChannelJsonWriter.hpp"

#include <system_error>

namespace hftrec::capture {

namespace {

Status closeStreamChecked(std::ofstream& stream) noexcept {
    if (!stream.is_open()) return Status::Ok;
    stream.flush();
    const bool flushed = stream.good();
    stream.close();
    return flushed && stream.good() ? Status::Ok : Status::IoError;
}

}  // namespace

Status ChannelJsonWriter::open(ChannelKind channel, const std::filesystem::path& sessionDir) noexcept {
    if (stream_.is_open()) {
        return Status::Ok;
    }
    channel_ = channel;
    return openRelativePath(sessionDir, std::string{channelJsonlRelativePath(channel)}, true);
}

Status ChannelJsonWriter::openRelativePath(const std::filesystem::path& sessionDir,
                                           const std::filesystem::path& relativePath,
                                           bool append) noexcept {
    if (stream_.is_open()) {
        return Status::Ok;
    }
    const auto path = sessionDir / relativePath;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Status::IoError;
    stream_.open(path, append ? (std::ios::out | std::ios::app) : std::ios::out);
    return stream_.is_open() ? Status::Ok : Status::IoError;
}

Status ChannelJsonWriter::writeLine(const std::string& jsonLine) noexcept {
    if (!stream_.is_open()) return Status::InvalidArgument;
    stream_ << jsonLine << '\n';
    stream_.flush();
    return stream_.good() ? Status::Ok : Status::IoError;
}

Status ChannelJsonWriter::writeJson(const std::string& jsonDocument) noexcept {
    if (!stream_.is_open()) return Status::InvalidArgument;
    stream_ << jsonDocument;
    stream_.flush();
    return stream_.good() ? Status::Ok : Status::IoError;
}

Status ChannelJsonWriter::close() noexcept {
    return closeStreamChecked(stream_);
}

}  // namespace hftrec::capture
