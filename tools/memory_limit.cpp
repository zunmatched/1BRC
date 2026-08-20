#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~UniqueHandle() {
        reset();
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::string windows_error(std::string_view operation) {
    return std::string(operation) + " (Win32 error " + std::to_string(GetLastError()) + ')';
}

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            wide.data(), required) == 0) {
        return {};
    }
    return wide;
}

[[nodiscard]] std::wstring quote_argument(std::wstring_view argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        }
        else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        }
        else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

[[nodiscard]] bool parse_limit(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value != 0 &&
           value <= std::numeric_limits<SIZE_T>::max() / (1024U * 1024U);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "usage: onebrc_memory_limit <MiB> <executable> <argument> [arguments...]\n";
        return 2;
    }

    std::uint64_t limit_mib = 0;
    if (!parse_limit(argv[1], limit_mib)) {
        std::cerr << "error: invalid memory limit\n";
        return 2;
    }

    std::wstring command_line;
    for (int index = 2; index < argc; ++index) {
        const auto wide = utf8_to_wide(argv[index]);
        if (wide.empty()) {
            std::cerr << "error: arguments must be valid UTF-8\n";
            return 2;
        }
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line += quote_argument(wide);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        std::cerr << "error: " << windows_error("cannot create Job Object") << '\n';
        return 1;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(limit_mib * 1024U * 1024U);
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0) {
        std::cerr << "error: " << windows_error("cannot configure Job Object") << '\n';
        return 1;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr,
                       &startup, &process) == 0) {
        std::cerr << "error: " << windows_error("cannot start child process") << '\n';
        return 1;
    }
    UniqueHandle process_handle(process.hProcess);
    UniqueHandle thread_handle(process.hThread);

    if (AssignProcessToJobObject(job.get(), process_handle.get()) == 0) {
        const auto message = windows_error("cannot assign child process to Job Object");
        TerminateProcess(process_handle.get(), 1);
        std::cerr << "error: " << message << '\n';
        return 1;
    }

    if (ResumeThread(thread_handle.get()) == std::numeric_limits<DWORD>::max()) {
        const auto message = windows_error("cannot resume child process");
        TerminateProcess(process_handle.get(), 1);
        std::cerr << "error: " << message << '\n';
        return 1;
    }
    thread_handle.reset();

    constexpr DWORD timeout_milliseconds = 10U * 60U * 1000U;
    const DWORD wait_result = WaitForSingleObject(process_handle.get(), timeout_milliseconds);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process_handle.get(), 1);
        WaitForSingleObject(process_handle.get(), 5'000U);
        std::cerr << "error: child process exceeded 10 minute timeout\n";
        return 1;
    }
    if (wait_result != WAIT_OBJECT_0) {
        std::cerr << "error: " << windows_error("cannot wait for child process") << '\n';
        return 1;
    }

    DWORD exit_code = 1;
    if (GetExitCodeProcess(process_handle.get(), &exit_code) == 0) {
        std::cerr << "error: " << windows_error("cannot read child exit code") << '\n';
        return 1;
    }
    return exit_code <= static_cast<DWORD>(std::numeric_limits<int>::max()) ? static_cast<int>(exit_code) : 1;
}
