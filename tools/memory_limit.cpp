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

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        std::cerr << "error: cannot create Job Object\n";
        return 1;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(limit_mib * 1024U * 1024U);
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0) {
        CloseHandle(job);
        std::cerr << "error: cannot configure Job Object\n";
        return 1;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr,
                       &startup, &process) == 0) {
        CloseHandle(job);
        std::cerr << "error: cannot start child process\n";
        return 1;
    }

    if (AssignProcessToJobObject(job, process.hProcess) == 0) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        std::cerr << "error: cannot assign child process to Job Object\n";
        return 1;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return exit_code <= static_cast<DWORD>(std::numeric_limits<int>::max()) ? static_cast<int>(exit_code) : 1;
}

