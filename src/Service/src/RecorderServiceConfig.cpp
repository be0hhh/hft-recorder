#include "hftrec/Service/RecorderService.hpp"
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include "cxet/Primitives/Buf/StringView.hpp"

namespace hftrec::service {
namespace {
cxet::StringView trim(cxet::StringView value) {
    while(!value.empty() && (value.front()==' ' || value.front()=='\t' || value.front()=='\r'))value.remove_prefix(1u);
    while(!value.empty() && (value.back()==' ' || value.back()=='\t' || value.back()=='\r'))value.remove_suffix(1u);
    return value;
}
}
Status loadRecorderServiceConfig(const std::filesystem::path& path,RecorderServiceConfig& output,std::string& error) noexcept {
    error.clear();output={};
    try {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file || file.tellg()<0 || file.tellg()>64u*1024u) {error="Recorder profile missing or exceeds 64KiB";return Status::IoError;}
        const auto size=static_cast<std::size_t>(file.tellg());file.seekg(0);
        std::string text(size,'\0');if(size && !file.read(text.data(),static_cast<std::streamsize>(size)))return Status::IoError;
        if(text.find('\0')!=std::string::npos) {error="Recorder profile contains binary data";return Status::InvalidArgument;}
        std::set<std::string> seen;cxet::StringView remaining{text.data(),text.size()};
        auto owned=[](cxet::StringView value){return std::string(value.data(),value.size());};
        auto fail=[&](cxet::StringView key) {error="invalid or duplicate Recorder profile field: "+owned(key);return Status::InvalidArgument;};
        while(!remaining.empty()) {
            const auto newline=remaining.find('\n');auto line=trim(remaining.substr(0,newline));
            if(newline==cxet::StringView::npos)remaining={};else remaining.remove_prefix(newline+1u);
            if(line.empty() || line.front()=='#' || line.front()==';')continue;
            const auto separator=line.find('=');if(separator==cxet::StringView::npos)return fail(line);
            const auto key=trim(line.substr(0,separator)),value=trim(line.substr(separator+1u));
            if(key=="venue") {
                if(output.venues.size()>=64u)return fail(key);
                const auto first=value.find('|'),last=first==cxet::StringView::npos?first:value.find('|',first+1u);
                if(first==cxet::StringView::npos || last==cxet::StringView::npos || value.find('|',last+1u)!=cxet::StringView::npos)return fail(key);
                RecorderVenue venue;venue.exchange=owned(trim(value.substr(0,first)));venue.market=owned(trim(value.substr(first+1u,last-first-1u)));
                auto symbols=trim(value.substr(last+1u));venue.fullUniverse=symbols=="*";
                if(venue.exchange.empty() || venue.market.empty() || symbols.empty() || venue.exchange.size()>48u || venue.market.size()>48u)return fail(key);
                if(!venue.fullUniverse)while(!symbols.empty()) {
                    const auto comma=symbols.find(',');const auto symbol=trim(symbols.substr(0,comma));
                    if(symbol.empty() || symbol.size()>128u || venue.instruments.size()>=4096u)return fail(key);
                    venue.instruments.push_back(owned(symbol));
                    if(comma==cxet::StringView::npos)symbols={};else {symbols.remove_prefix(comma+1u);if(symbols.empty())return fail(key);}
                }
                output.venues.push_back(std::move(venue));continue;
            }
            if(!seen.insert(owned(key)).second)return fail(key);
            if(key=="workspaceRoot")output.workspaceRoot=owned(value);
            else if(key=="parserTemplate")output.parserTemplate=owned(value);
            else if(key=="envPath")output.envPath=owned(value);
            else if(key=="outputRoot")output.outputRoot=owned(value);
            else {
                std::uint64_t number{};const auto conversion=std::from_chars(value.data(),value.data()+value.size(),number);
                if(conversion.ec!=std::errc{} || conversion.ptr!=value.data()+value.size())return fail(key);
                if(key=="channelMask") {if(number>255u)return fail(key);output.channelMask=static_cast<std::uint16_t>(number);}
                else if(key=="durationSec")output.durationSec=number;
                else if(key=="maximumBytes")output.maximumBytes=number;
                else if(key=="segmentBytes")output.segmentBytes=number;
                else if(key=="retainedCompressedBytes")output.retainedCompressedBytes=number;
                else if(key=="maximumReferenceBytes")output.maximumReferenceBytes=number;
                else if(key=="writerQueueRecords") {if(number>65536u)return fail(key);output.writerQueueRecords=static_cast<std::uint32_t>(number);}
                else return fail(key);
            }
        }
        if(output.workspaceRoot.empty() || output.parserTemplate.empty() || output.envPath.empty() || output.outputRoot.empty() || output.venues.empty()) {
            error="Recorder profile requires workspaceRoot, parserTemplate, envPath, outputRoot and venue";return Status::InvalidArgument;
        }
        return Status::Ok;
    } catch(...) {error="Recorder profile load failed";return Status::IoError;}
}
} // namespace hftrec::service
