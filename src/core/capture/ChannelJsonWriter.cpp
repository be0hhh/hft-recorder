#include "core/capture/ChannelJsonWriter.hpp"

#include <system_error>

namespace hftrec::capture {

Status ChannelJsonWriter::open(ChannelKind channel, const std::filesystem::path& sessionDir) noexcept {
    if (stream_.is_open()) {
        return Status::Ok;
    }
    channel_ = channel;
    const auto path = sessionDir / std::string{channelJsonlRelativePath(channel)};
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Status::IoError;
    stream_.open(path, channel == ChannelKind::Snapshot ? std::ios::out : (std::ios::out | std::ios::app));
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
    if (stream_.is_open()) stream_.close();
    return Status::Ok;
}

}  // namespace hftrec::capture
