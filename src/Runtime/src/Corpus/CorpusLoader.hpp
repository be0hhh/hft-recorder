#pragma once

#include <filesystem>

#include "../Common/Status.hpp"
#include "LoadReport.hpp"
#include "SessionCorpus.hpp"

namespace hftrec::corpus {

class CorpusLoader {
  public:
    Status loadDetailed(const std::filesystem::path& sessionDir,
                        SessionCorpus& out,
                        LoadReport& report) noexcept;
    Status load(const std::filesystem::path& sessionDir, SessionCorpus& out) noexcept;
};

}  // namespace hftrec::corpus
