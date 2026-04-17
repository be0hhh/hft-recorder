#pragma once

#include <filesystem>

#include "core/common/Status.hpp"
#include "core/corpus/SessionCorpus.hpp"

namespace hftrec::corpus {

class CorpusLoader {
  public:
    Status load(const std::filesystem::path& sessionDir, SessionCorpus& out) noexcept;
};

}  // namespace hftrec::corpus
