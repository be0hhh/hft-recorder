#include "SessionId.hpp"

#include <limits.h>

#include "Corpus/Recordings/RecordingDiscovery.hpp"

namespace hftrec::capture {

std::string makeSessionId(const std::string& exchange,
                          const std::string& market,
                          const std::string& symbolOrBasket,
                          long long timestampSuffix) noexcept {
    try {
        const auto validName=[](const std::string& value) {
            if(value.empty() || value.size()>=NAME_MAX) return false;
            for(const auto ch:value)
                if(!((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') ||
                     (ch>='0' && ch<='9') || ch=='_' || ch=='-')) return false;
            return true;
        };
        if(timestampSuffix<=0 || !validName(exchange) || !validName(market)) return {};
        const auto folderSymbol=hftrec::recordings::recordingFolderSymbol(symbolOrBasket);
        if(folderSymbol.empty()) return {};
        auto result=std::to_string(timestampSuffix)+"_"+exchange+"_"+market+"_"+folderSymbol;
        return result.size()>NAME_MAX?std::string{}:result;
    } catch(...) {return {};}
}

}  // namespace hftrec::capture
