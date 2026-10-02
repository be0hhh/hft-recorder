#include "RecorderTuiInternal.hpp"

namespace hftrec::app::tui_detail {

volatile std::sig_atomic_t gInterrupted = 0;

void handleSignal(int) {
    gInterrupted = 1;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1u]))) --end;
    return std::string{text.substr(begin, end - begin)};
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

std::int64_t wallNowNs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

std::filesystem::path uniquePath(const std::filesystem::path& parent, const std::string& baseName) {
    std::filesystem::path candidate = parent / baseName;
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    for (int i = 2; i < 1000; ++i) {
        std::ostringstream suffix;
        suffix << baseName << '_' << (i < 10 ? "0" : "") << i;
        candidate = parent / suffix.str();
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return parent / (baseName + "_overflow");
}

Key readKey(int timeoutMs) {
    pollfd pfd{STDIN_FILENO, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, timeoutMs);
    if (ready <= 0 || (pfd.revents & POLLIN) == 0) return {};

    char ch = 0;
    if (::read(STDIN_FILENO, &ch, 1) != 1) return {};
    if (ch == '\r' || ch == '\n') return {KeyKind::Enter, 0};
    if (ch == 0x7f || ch == '\b') return {KeyKind::Backspace, 0};
    if (ch == 0x1b) {
        char seq[2]{};
        if (::read(STDIN_FILENO, &seq[0], 1) != 1) return {KeyKind::Escape, 0};
        if (::read(STDIN_FILENO, &seq[1], 1) != 1) return {KeyKind::Escape, 0};
        if (seq[0] == '[') {
            if (seq[1] == 'A') return {KeyKind::Up, 0};
            if (seq[1] == 'B') return {KeyKind::Down, 0};
            if (seq[1] == 'C') return {KeyKind::Right, 0};
            if (seq[1] == 'D') return {KeyKind::Left, 0};
        }
        return {KeyKind::Escape, 0};
    }
    if (ch == 3) {
        gInterrupted = 1;
        return {};
    }
    return {KeyKind::Character, ch};
}

void clearScreen() {
    std::fputs("\033[2J\033[H", stdout);
}

tui::TerminalViewport currentViewport() noexcept {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0 && size.ws_col > 0) {
        return tui::sanitizeViewport(
            tui::TerminalViewport{.rows = static_cast<int>(size.ws_row), .cols = static_cast<int>(size.ws_col)});
    }
    return tui::sanitizeViewport({});
}

void printLine(std::string_view line, tui::TerminalViewport viewport) {
    const std::string text = tui::truncateForTerminal(line, viewport.cols);
    std::fputs(text.c_str(), stdout);
    std::putchar('\n');
}

std::string promptLine(TerminalGuard& terminal, std::string_view label, std::string_view current) {
    terminal.suspend();
    std::fputs("\033[?25h", stdout);
    std::printf("\n%s", std::string(label).c_str());
    if (!current.empty()) std::printf(" [%s]", std::string(current).c_str());
    std::printf(": ");
    std::fflush(stdout);

    std::string line;
    std::getline(std::cin, line);
    std::fputs("\033[?25l", stdout);
    terminal.resume();
    line = trim(line);
    return line.empty() ? std::string{current} : line;
}

}  // namespace hftrec::app::tui_detail
