#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/finam/FinamEnvSync.hpp"

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    out << text;
}

fs::path uniqueRoot(const char* suffix) {
    return fs::temp_directory_path() /
           (std::string{"hftrec_finam_env_sync_"} + suffix + "_" + std::to_string(std::rand()));
}

TEST(FinamEnvSync, UpdatesJwtAndPreservesExistingEnvLines) {
    const fs::path root = uniqueRoot("preserve");
    const fs::path envPath = root / "custom.env";
    writeFile(envPath,
              "# comment stays\n"
              "FINAM_API_1_SECRET=secret-value\n"
              "FINAM_API_1_JWT=old-token\n"
              "OTHER_KEY=untouched\n");

    hftrec::finam::FinamEnvSyncRequest request{};
    request.envPath = envPath;
    request.apiSlot = 1u;
    request.mirrorStandardEnv = false;

    hftrec::finam::FinamEnvValues values{};
    values.jwt = "new-token";
    values.accountId = "2068089";
    hftrec::finam::FinamEnvSyncResult result{};

    ASSERT_EQ(hftrec::finam::writeFinamEnvValues(request, values, &result), hftrec::Status::Ok)
        << result.error;
    EXPECT_TRUE(result.persisted);
    ASSERT_EQ(result.writtenPaths.size(), 1u);

    const std::string text = readFile(envPath);
    EXPECT_NE(text.find("# comment stays\n"), std::string::npos);
    EXPECT_NE(text.find("FINAM_API_1_SECRET=secret-value\n"), std::string::npos);
    EXPECT_NE(text.find("FINAM_API_1_JWT=new-token\n"), std::string::npos);
    EXPECT_NE(text.find("FINAM_API_1_ACCOUNT_ID=2068089\n"), std::string::npos);
    EXPECT_NE(text.find("OTHER_KEY=untouched\n"), std::string::npos);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(FinamEnvSync, MirrorsStandardRecorderAndTraderEnvFiles) {
    const fs::path root = uniqueRoot("mirror");
    const fs::path recorderEnv = root / "apps" / "hft-recorder" / ".env";
    const fs::path traderEnv = root / "apps" / "hft-trader" / ".env";
    writeFile(recorderEnv, "FINAM_API_1_JWT=recorder-old\n");
    writeFile(traderEnv, "FINAM_API_1_JWT=trader-old\n");

    hftrec::finam::FinamEnvSyncRequest request{};
    request.envPath = recorderEnv;
    request.apiSlot = 1u;
    request.mirrorStandardEnv = true;

    hftrec::finam::FinamEnvValues values{};
    values.jwt = "shared-new-token";
    hftrec::finam::FinamEnvSyncResult result{};

    ASSERT_EQ(hftrec::finam::writeFinamEnvValues(request, values, &result), hftrec::Status::Ok)
        << result.error;
    EXPECT_TRUE(result.persisted);
    EXPECT_EQ(result.writtenPaths.size(), 2u);
    EXPECT_NE(readFile(recorderEnv).find("FINAM_API_1_JWT=shared-new-token\n"), std::string::npos);
    EXPECT_NE(readFile(traderEnv).find("FINAM_API_1_JWT=shared-new-token\n"), std::string::npos);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(FinamEnvSync, RejectsMissingJwtValue) {
    const fs::path root = uniqueRoot("missing");
    hftrec::finam::FinamEnvSyncRequest request{};
    request.envPath = root / ".env";
    hftrec::finam::FinamEnvValues values{};
    hftrec::finam::FinamEnvSyncResult result{};

    EXPECT_EQ(hftrec::finam::writeFinamEnvValues(request, values, &result), hftrec::Status::InvalidArgument);
    EXPECT_FALSE(result.persisted);
    EXPECT_NE(result.error.find("missing Finam JWT"), std::string::npos);

    std::error_code ec;
    fs::remove_all(root, ec);
}

}  // namespace
