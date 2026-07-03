#include <gtest/gtest.h>

#include <filesystem>

#include "core/tui/status/RecorderTuiShardStatus.hpp"

namespace {

std::filesystem::path testStatusPath(const testing::TestInfo* info) {
    auto path = std::filesystem::temp_directory_path();
    path /= "hftrec_shard_status_tests";
    path /= info->test_suite_name();
    path /= info->name();
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path / "shard01.status";
}

}  // namespace

TEST(RecorderTuiShardStatus, PersistsExitSignalAsFailure) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "running";
    status.jobs = 1;
    status.running = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::signaled(11));

    EXPECT_EQ(status.state, "exited");
    EXPECT_EQ(status.message, "signal=11");
    EXPECT_EQ(status.signal, 11);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, PersistsNonZeroExitAsFailure) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "running";
    status.jobs = 1;
    status.running = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(2));

    EXPECT_EQ(status.state, "exited");
    EXPECT_EQ(status.message, "exit=2");
    EXPECT_EQ(status.exitCode, 2);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, CleanExitWithStaleRunningStatusIsIncomplete) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "running";
    status.jobs = 3;
    status.running = 3;
    status.finalized = 0;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "exited_incomplete");
    EXPECT_EQ(status.message, "exit=0 before final status");
    EXPECT_EQ(status.exitCode, 0);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, CleanExitKeepsFinalDoneStatus) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "done";
    status.message = "done";
    status.jobs = 2;
    status.finalized = 2;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "done");
    EXPECT_EQ(status.message, "exit=0");
    EXPECT_EQ(status.errors, 0);
}

TEST(RecorderTuiShardStatus, CleanExitKeepsFinalFailedEmptyStatus) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "failed_empty";
    status.message = "connect_failed tcp_connect";
    status.jobs = 1;
    status.finalized = 1;
    status.errors = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "failed_empty");
    EXPECT_EQ(status.message, "connect_failed tcp_connect");
    EXPECT_EQ(status.exitCode, 0);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, CleanExitKeepsFinalPreflightFailedStatus) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "preflight_failed";
    status.message = "preflight: trades:connect_failed(tcp_connect)";
    status.jobs = 1;
    status.finalized = 1;
    status.errors = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "preflight_failed");
    EXPECT_EQ(status.message, "preflight: trades:connect_failed(tcp_connect)");
    EXPECT_EQ(status.exitCode, 0);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, ErrorAndPreflightFailedAreFinalShardStates) {
    hftrec::tui::RecorderTuiShardStatus errorStatus{};
    errorStatus.state = "error";
    errorStatus.jobs = 1;
    errorStatus.finalized = 1;
    errorStatus.errors = 1;

    hftrec::tui::RecorderTuiShardStatus preflightStatus{};
    preflightStatus.state = "preflight_failed";
    preflightStatus.jobs = 1;
    preflightStatus.finalized = 1;
    preflightStatus.errors = 1;

    EXPECT_TRUE(hftrec::tui::shardStatusIsFinal(errorStatus));
    EXPECT_TRUE(hftrec::tui::shardStatusIsFinal(preflightStatus));
}

TEST(RecorderTuiShardStatus, CleanExitConvertsStaleStoppingStatusToStopped) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "stopping";
    status.message = "stop requested";
    status.jobs = 1;
    status.running = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "stopped");
    EXPECT_EQ(status.message, "exit=0");
    EXPECT_EQ(status.running, 0);
    EXPECT_EQ(status.starting, 0);
    EXPECT_EQ(status.pending, 0);
    EXPECT_EQ(status.exitCode, 0);
    EXPECT_EQ(status.errors, 0);
}

TEST(RecorderTuiShardStatus, CleanExitPreservesStoppedErrorMessage) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "stopped";
    status.message = "connect_failed fix-md";
    status.jobs = 1;
    status.finalized = 1;
    status.errors = 1;

    hftrec::tui::applyShardChildExit(status, hftrec::tui::RecorderTuiShardExit::exited(0));

    EXPECT_EQ(status.state, "stopped");
    EXPECT_EQ(status.message, "connect_failed fix-md");
    EXPECT_EQ(status.exitCode, 0);
    EXPECT_EQ(status.errors, 1);
}

TEST(RecorderTuiShardStatus, WritesAndReadsAdditiveStatusFields) {
    const auto path = testStatusPath(testing::UnitTest::GetInstance()->current_test_info());
    hftrec::tui::RecorderTuiShardStatus status{};
    status.state = "skipped";
    status.message = "all jobs skipped: no supported channels";
    status.jobs = 4;
    status.finalized = 4;
    status.skipped = 4;
    status.rows = 0;
    status.pid = 12345;
    status.updatedWallNs = 987654321;
    status.firstError = "trades connect_failed";
    status.lastError = "bookticker subscribe_failed";

    ASSERT_TRUE(hftrec::tui::writeShardStatusFile(path, status));

    const auto read = hftrec::tui::readShardStatusFile(path);
    EXPECT_TRUE(read.statusFilePresent);
    EXPECT_EQ(read.state, "skipped");
    EXPECT_EQ(read.message, "all jobs skipped: no supported channels");
    EXPECT_EQ(read.jobs, 4);
    EXPECT_EQ(read.finalized, 4);
    EXPECT_EQ(read.skipped, 4);
    EXPECT_EQ(read.pid, 12345);
    EXPECT_EQ(read.updatedWallNs, 987654321);
    EXPECT_EQ(read.firstError, "trades connect_failed");
    EXPECT_EQ(read.lastError, "bookticker subscribe_failed");
}

TEST(RecorderTuiShardStatus, FailedEmptyIsRenderedAsIssue) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.statusFilePresent = true;
    status.state = "failed_empty";
    status.message = "connect_failed tcp_connect";
    status.jobs = 1;
    status.finalized = 1;
    status.errors = 1;

    EXPECT_TRUE(hftrec::tui::shardStatusIsIssue(status));
}

TEST(RecorderTuiShardStatus, PreflightFailedIsRenderedAsIssue) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.statusFilePresent = true;
    status.state = "preflight_failed";
    status.message = "preflight: trades:connect_failed(tcp_connect)";
    status.jobs = 1;
    status.finalized = 1;
    status.errors = 1;

    EXPECT_TRUE(hftrec::tui::shardStatusIsIssue(status));
}

TEST(RecorderTuiShardStatus, KillSentNotReapedIsRenderedAsIssue) {
    hftrec::tui::RecorderTuiShardStatus status{};
    status.statusFilePresent = true;
    status.state = "kill_sent_not_reaped";
    status.message = "SIGKILL sent but waitpid did not reap";
    status.jobs = 1;
    status.signal = 9;

    EXPECT_TRUE(hftrec::tui::shardStatusIsIssue(status));
}
