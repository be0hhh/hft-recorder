#include "RecordingRoot.hpp"

#include <cstdlib>
#include <limits.h>
#include <optional>
#include <string>

namespace hftrec::recordings {
namespace {

std::filesystem::path portablePath(const std::filesystem::path& input) {
    auto text=input.string();
    if(text.empty() || text.size()>=PATH_MAX || text.find('\0')!=std::string::npos) return {};
    for(auto& ch:text) if(ch=='\\') ch='/';
    if(text.size()>=2u && text[1]==':') {
        auto drive=text[0];
        if(drive>='A' && drive<='Z') drive=static_cast<char>(drive-'A'+'a');
        if(drive<'a' || drive>'z') return {};
        auto suffix=text.substr(2u);
        // The recorded legacy D:recordings spelling is explicitly supported;
        // other Windows drive-relative paths need a drive working directory.
        if(suffix.empty() || (suffix.front()!='/' &&
           !(drive=='d' && (suffix=="recordings" || suffix.starts_with("recordings/"))))) return {};
        if(suffix.front()=='/') suffix.erase(0u,1u);
        text=std::string{"/mnt/"}+drive+"/"+suffix;
    }
    return std::filesystem::path{text}.lexically_normal();
}

std::optional<std::filesystem::path> legacyTail(const std::filesystem::path& path,bool saved) {
    if(!path.is_absolute()) {
        auto first=path.begin();
        if(first!=path.end() && *first=="recordings") {
            std::filesystem::path tail;
            for(++first;first!=path.end();++first) tail/=*first;
            return tail;
        }
    }
    for(auto first=path.begin();first!=path.end();++first) {
        if(*first!="apps" || (!saved && (path.is_absolute() || first!=path.begin()))) continue;
        auto owner=first;++owner;
        if(owner==path.end() || *owner!="hft-recorder") continue;
        auto directory=owner;++directory;
        if(directory==path.end() || *directory!="recordings") continue;
        std::filesystem::path tail;
        for(++directory;directory!=path.end();++directory) tail/=*directory;
        return tail;
    }
    if(saved) {
        const auto relative=path.lexically_relative("/mnt/d/recordings");
        if(!relative.empty() && *relative.begin()!="..") return relative=="."?std::filesystem::path{}:relative;
    }
    return std::nullopt;
}

std::filesystem::path normalizePath(const std::filesystem::path& input,bool saved) {
    const auto path=portablePath(input);
    if(path.empty()) return {};
    if(const auto tail=legacyTail(path,saved)) {
        const auto root=defaultRecordingsRoot();
        return root.empty()?std::filesystem::path{}:(root/ *tail).lexically_normal();
    }
    return path;
}

}  // namespace

std::filesystem::path defaultRecordingsRoot() noexcept {
    try {
        const auto* configured=std::getenv("HFTREC_RECORDINGS_ROOT");
        return configured && configured[0]!='\0'?portablePath(configured):std::filesystem::path{"/mnt/d/recordings"};
    } catch(...) {return {};}
}

std::filesystem::path normalizeRecordingsPath(const std::filesystem::path& path) noexcept {
    try {return normalizePath(path,true);} catch(...) {return {};}
}

std::filesystem::path normalizeExplicitRecordingsPath(const std::filesystem::path& path) noexcept {
    try {return normalizePath(path,false);} catch(...) {return {};}
}

}  // namespace hftrec::recordings
