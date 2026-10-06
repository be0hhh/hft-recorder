#include "RecorderCaptureSession.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace hftrec::capture {
namespace {
std::string trim(std::string_view input) {
    while(!input.empty() && (input.front()==' ' || input.front()=='\t' || input.front()=='\r')) input.remove_prefix(1);
    while(!input.empty() && (input.back()==' ' || input.back()=='\t' || input.back()=='\r')) input.remove_suffix(1);
    return std::string(input);
}
std::string lower(std::string input) {
    for(auto& c:input) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return input;
}
struct Section {std::string name;std::vector<std::pair<std::string,std::string>> values;};
std::string value(const Section& section,std::string_view key) {
    for(const auto& pair:section.values) if(pair.first==key) return pair.second;
    return {};
}
bool safeToken(std::string_view text,std::size_t maximum) {
    if(text.empty() || text.size()>maximum) return false;
    for(const auto c:text)
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
             c=='_' || c=='/' || c=='@' || c=='.' || c=='-' || c==':')) return false;
    return true;
}
bool parseSections(std::string_view text,std::vector<Section>& sections) {
    if(text.empty() || text.size()>1024u*1024u) return false;
    while(!text.empty()) {
        const auto end=text.find('\n');auto line=trim(text.substr(0,end));
        if(end==std::string_view::npos) text={};else text.remove_prefix(end+1u);
        if(line.empty() || line.front()=='#' || line.front()==';') continue;
        if(line.front()=='[' && line.back()==']') {
            if(sections.size()>=512u) return false;
            const auto name=trim(std::string_view(line).substr(1,line.size()-2u));
            if(!safeToken(name,128u) || std::any_of(sections.begin(),sections.end(),[&](const auto& s){return s.name==name;})) return false;
            sections.push_back({name,{}});continue;
        }
        const auto equals=line.find('=');
        if(sections.empty() || equals==std::string::npos || sections.back().values.size()>=64u) return false;
        auto key=trim(std::string_view(line).substr(0,equals));
        auto v=trim(std::string_view(line).substr(equals+1u));
        if(!safeToken(key,128u) || v.size()>16u*1024u || v.find('\0')!=std::string::npos ||
            std::any_of(sections.back().values.begin(),sections.back().values.end(),[&](const auto& p){return p.first==key;})) return false;
        sections.back().values.emplace_back(std::move(key),std::move(v));
    }
    return !sections.empty();
}
std::vector<std::string> commaValues(std::string_view text) {
    std::vector<std::string> output;
    while(!text.empty()) {
        const auto end=text.find(',');auto item=trim(text.substr(0,end));
        if(!item.empty()) output.push_back(std::move(item));
        if(end==std::string_view::npos) break;
        text.remove_prefix(end+1u);
    }
    return output;
}
void appendSection(std::string& output,const Section& section) {
    output+='[';output+=section.name;output+="]\n";
    for(const auto& pair:section.values) output+=pair.first+"="+pair.second+"\n";
    output+='\n';
}
} // namespace

Status renderRecorderParserConfig(std::string_view parserTemplate,
    const RecorderCaptureSessionConfig& config,std::string& output,std::string& error) noexcept {
    output.clear();error.clear();
    try {
        constexpr std::uint16_t supported=corpus::binaryMarketChannelBit(corpus::BinaryMarketChannel::BookTicker)|
            corpus::binaryMarketChannelBit(corpus::BinaryMarketChannel::Trade)|corpus::binaryMarketChannelBit(corpus::BinaryMarketChannel::Depth);
        if(config.venues.empty() || config.venues.size()>64u || config.channelMask==0u ||
           (config.channelMask&~std::uint16_t{0xffu})!=0u || config.maximumBytes==0u ||
           config.durationSec==0u || config.durationSec>std::numeric_limits<std::uint64_t>::max()/1'000'000'000u) {
            error="invalid Recorder capture selection or storage limits";return Status::InvalidArgument;
        }
        if((config.channelMask&~supported)!=0u) {
            error="selected channels have no current Parser template subscription boundary";return Status::Unimplemented;
        }
        std::vector<Section> sections;
        if(!parseSections(parserTemplate,sections)) {error="invalid or oversized Parser template";return Status::CorruptData;}
        std::vector<Section> selected;
        std::unordered_set<std::string> bindings;
        std::unordered_set<std::string> identities;
        for(const auto& requested:config.venues) {
            if(!safeToken(requested.exchange,16u) || !safeToken(requested.market,16u) ||
                requested.instruments.size()>4096u ||
                (requested.fullUniverse && !requested.instruments.empty()) ||
                (!requested.fullUniverse && requested.instruments.empty())) {
                error="venue selection needs exact instruments or an explicit full universe";return Status::InvalidArgument;
            }
            const auto identity=lower(requested.exchange+"/"+requested.market);
            if(!identities.insert(identity).second) {error="duplicate Recorder venue selection";return Status::InvalidArgument;}
            const auto found=std::find_if(sections.begin(),sections.end(),[&](const auto& s){return lower(s.name)=="venue."+identity;});
            if(found==sections.end()) {error="selected venue has no Parser-owned template route: "+identity;return Status::Unimplemented;}
            Section venue{found->name,{}};
            bool reference=false;
            for(const auto& pair:found->values) {
                const bool bbo=pair.first=="bbo_binding",trade=pair.first=="trade_binding",depth=pair.first=="depth_binding";
                if((bbo && (config.channelMask&1u)==0u) || (trade && (config.channelMask&2u)==0u) ||
                    (depth && (config.channelMask&4u)==0u) || pair.first=="whitelist_instruments" ||
                    (pair.first=="snapshot_binding" && (config.channelMask&4u)==0u) ||
                    pair.first=="user_binding" || pair.first=="order_binding" ||
                    pair.first=="candle_history" || pair.first=="candle_backfill" || pair.first=="universe_scope") continue;
                venue.values.push_back(pair);
                if(bbo || trade || depth || pair.first=="reference_binding" || pair.first=="snapshot_binding") bindings.insert(pair.second);
                reference=reference || pair.first=="reference_binding";
            }
            if(!reference || ((config.channelMask&1u)!=0u && value(venue,"bbo_binding").empty()) ||
               ((config.channelMask&2u)!=0u && value(venue,"trade_binding").empty()) ||
               ((config.channelMask&4u)!=0u && value(venue,"depth_binding").empty())) {
                error="selected channel has no Parser-owned binding for "+identity;return Status::Unimplemented;
            }
            venue.values.emplace_back("universe_scope","all");
            if(!requested.fullUniverse) {
                std::string whitelist;std::unordered_set<std::string> instruments;
                for(const auto& symbol:requested.instruments) {
                    if(!safeToken(symbol,corpus::kBinaryMarketSymbolBytes) || !instruments.insert(symbol).second) {
                        error="invalid or duplicate canonical instrument selection";return Status::InvalidArgument;
                    }
                    if(!whitelist.empty()) whitelist+=',';whitelist+=symbol;
                }
                venue.values.emplace_back("whitelist_instruments",std::move(whitelist));
            }
            selected.push_back(std::move(venue));
        }
        for(const auto& section:sections) {
            if(section.name=="runtime" || section.name.starts_with("algorithm.")) appendSection(output,section);
        }
        if(std::none_of(sections.begin(),sections.end(),[](const auto& section){return section.name=="runtime";})) {output.clear();error="Parser template lacks runtime policy";return Status::CorruptData;}
        for(const auto& section:selected) appendSection(output,section);
        for(const auto& name:bindings) {
            const auto found=std::find_if(sections.begin(),sections.end(),[&](const auto& s){return s.name=="connection."+name;});
            if(found==sections.end()) {output.clear();error="Parser template binding has no connection: "+name;return Status::CorruptData;}
            Section connection=*found;
            for(auto& pair:connection.values) if(pair.first=="streams") {
                auto streams=commaValues(pair.second);std::string retained;
                for(const auto& stream:streams) {
                    const bool enabled=(stream=="Bbo" && (config.channelMask&1u)!=0u) ||
                        ((stream=="Trade" || stream=="AggregateTrade") && (config.channelMask&2u)!=0u) ||
                        ((stream=="Depth" || stream=="OrderBook") && (config.channelMask&4u)!=0u);
                    if(enabled) {if(!retained.empty()) retained+=',';retained+=stream;}
                }
                if(retained.empty()) {output.clear();error="selected Parser connection has no requested stream";return Status::Unimplemented;}
                pair.second=std::move(retained);
            }
            appendSection(output,connection);
        }
        return Status::Ok;
    } catch(...) {output.clear();error="Recorder capture configuration allocation failed";return Status::IoError;}
}
Status planRecorderSubscriptionChanges(const RecorderCaptureSessionConfig& desired,
    std::span<const corpus::BinaryMarketSource> directory,std::vector<RecorderSubscriptionChange>& changes,
    std::string& error) noexcept {
    changes.clear();error.clear();
    try {
        if(desired.venues.empty() || desired.venues.size()>64u || desired.channelMask==0u ||
           directory.size()>corpus::kBinaryMarketMaximumSources) {error="invalid bounded dynamic capture selection";return Status::InvalidArgument;}
        if((desired.channelMask&~std::uint16_t{7u})!=0u) {error="selected dynamic channel has no supported Parser control boundary";return Status::Unimplemented;}
        std::vector<std::uint16_t> wanted(directory.size(),0u);
        for(std::size_t i=0u;i<directory.size();++i) {
            const auto& source=directory[i];
            if(source.sourceId!=i+1u || !corpus::validBinaryMarketText(source.venue,source.venueBytes) ||
               !corpus::validBinaryMarketText(source.market,source.marketBytes) ||
               !corpus::validBinaryMarketText(source.canonicalSymbol,source.canonicalSymbolBytes)) {
                error="invalid current Parser source identities";return Status::CorruptData;
            }
        }
        std::unordered_set<std::string> venues;
        for(const auto& venue:desired.venues) {
            const auto identity=lower(venue.exchange+"/"+venue.market);
            if(!safeToken(venue.exchange,16u) || !safeToken(venue.market,16u) || !venues.insert(identity).second ||
               (venue.fullUniverse && !venue.instruments.empty()) || (!venue.fullUniverse && venue.instruments.empty())) {
                error="invalid or duplicate dynamic venue selection";return Status::InvalidArgument;
            }
            bool matchedVenue=false;std::vector<bool> matchedSymbols(venue.instruments.size(),false);
            for(std::size_t i=0u;i<directory.size();++i) {
                const auto& source=directory[i];
                const std::string exchange{source.venue.data(),source.venueBytes},market{source.market.data(),source.marketBytes};
                if(lower(exchange+"/"+market)!=identity) continue;
                matchedVenue=true;
                if(venue.fullUniverse) {wanted[i]=desired.channelMask;continue;}
                const std::string symbol{source.canonicalSymbol.data(),source.canonicalSymbolBytes};
                for(std::size_t n=0u;n<venue.instruments.size();++n)
                    if(venue.instruments[n]==symbol) {matchedSymbols[n]=true;wanted[i]=desired.channelMask;}
            }
            if(!matchedVenue || std::any_of(matchedSymbols.begin(),matchedSymbols.end(),[](bool matched){return !matched;})) {
                error="requested instrument has no existing producer source identity; live catalog admission is required";
                changes.clear();return Status::Unimplemented;
            }
        }
        // Remove unwanted feeds before adding new ones. Native capability and
        // multiplex/depth admission are checked by the producer, never guessed.
        for(bool add:{false,true}) for(std::size_t i=0u;i<directory.size();++i) {
            const auto differing=add?std::uint16_t(wanted[i]&~directory[i].availableChannelMask):
                std::uint16_t(directory[i].availableChannelMask&~wanted[i]);
            for(std::uint8_t c=1u;c<=3u;++c)
                if((differing&(std::uint16_t{1u}<<(c-1u)))!=0u)
                    changes.push_back({directory[i].sourceId,static_cast<corpus::BinaryMarketChannel>(c),add});
        }
        return Status::Ok;
    } catch(...) {changes.clear();error="dynamic capture selection allocation failed";return Status::IoError;}
}
} // namespace hftrec::capture
