#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifdef ONEBRC_MMAP
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#endif

namespace {

constexpr std::size_t maximum_station_count = 10'000;

enum class ExitCode : int {
    success = 0,
    failure = 1,
    usage = 2,
    allocation_failure = 3,
};

struct Stats {
    std::int32_t minimum = std::numeric_limits<std::int32_t>::max();
    std::int32_t maximum = std::numeric_limits<std::int32_t>::min();
    std::int64_t sum = 0;
    std::uint64_t count = 0;

    void add(std::int32_t temperature) {
        minimum = std::min(minimum, temperature);
        maximum = std::max(maximum, temperature);
        sum += temperature;
        ++count;
    }
};

#ifdef ONEBRC_NO_ROW_ALLOCATIONS
struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
        return left == right;
    }
};

using StationMap = std::unordered_map<std::string, Stats, TransparentStringHash, TransparentStringEqual>;
#else
using StationMap = std::unordered_map<std::string, Stats>;
#endif

#ifdef ONEBRC_NO_ROW_ALLOCATIONS
void process_record(std::string_view record, StationMap& stations, std::uint64_t line_number);
#else
void process_record(const std::string& record, StationMap& stations, std::uint64_t line_number);
#endif

#ifdef ONEBRC_INTEGER_PARSER
[[nodiscard]] std::int32_t parse_temperature(std::string_view text, std::uint64_t line_number) {
    const auto invalid = [line_number]() {
        throw std::runtime_error("invalid temperature on line " + std::to_string(line_number));
    };

    std::size_t index = 0;
    bool negative = false;
    if (!text.empty() && text.front() == '-') {
        negative = true;
        ++index;
    }
    const auto digits_before_decimal = text.size() - index >= 2 ? text.size() - index - 2 : 0;
    if (digits_before_decimal < 1 || digits_before_decimal > 2 || text[text.size() - 2] != '.') {
        invalid();
    }

    std::int32_t value = 0;
    for (std::size_t digit = 0; digit < digits_before_decimal; ++digit) {
        const char character = text[index + digit];
        if (character < '0' || character > '9') {
            invalid();
        }
        value = value * 10 + (character - '0');
    }
    const char fractional = text.back();
    if (fractional < '0' || fractional > '9') {
        invalid();
    }
    value = value * 10 + (fractional - '0');
    if (value > 999) {
        invalid();
    }
    return negative ? -value : value;
}
#else
[[nodiscard]] std::int32_t parse_temperature(const std::string& text, std::uint64_t line_number) {
    std::size_t parsed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &parsed);
    }
    catch (const std::exception&) {
        throw std::runtime_error("invalid temperature on line " + std::to_string(line_number));
    }

    if (parsed != text.size() || text.size() < 3 || text[text.size() - 2] != '.' || value < -99.9 ||
        value > 99.9) {
        throw std::runtime_error("invalid temperature on line " + std::to_string(line_number));
    }
    return static_cast<std::int32_t>(std::llround(value * 10.0));
}
#endif

[[nodiscard]] std::int64_t rounded_mean_tenths(const Stats& stats) {
    // Exact floor(sum / count + 0.5), including negative half ties toward positive infinity.
    const auto denominator = static_cast<std::int64_t>(stats.count * 2U);
    const auto numerator = stats.sum * 2 + static_cast<std::int64_t>(stats.count);
    auto quotient = numerator / denominator;
    if (numerator % denominator < 0) {
        --quotient;
    }
    return quotient;
}

void print_temperature(std::ostream& output, std::int64_t tenths) {
    const auto magnitude = tenths < 0 ? -tenths : tenths;
    if (tenths < 0) {
        output << '-';
    }
    output << magnitude / 10 << '.' << magnitude % 10;
}

[[nodiscard]] bool unsigned_byte_less(const std::string& left, const std::string& right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                        [](char a, char b) {
                                            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
                                        });
}

#ifdef ONEBRC_NO_ROW_ALLOCATIONS
void process_record(std::string_view record, StationMap& stations, std::uint64_t line_number) {
    if (!record.empty() && record.back() == '\r') {
        record.remove_suffix(1);
    }

    const auto separator = record.find(';');
    if (separator == std::string_view::npos || separator == 0 || separator > 100 ||
        record.find(';', separator + 1) != std::string_view::npos) {
        throw std::runtime_error("invalid record on line " + std::to_string(line_number));
    }

    const auto temperature = parse_temperature(record.substr(separator + 1), line_number);
    const auto station_name = record.substr(0, separator);
    auto station = stations.find(station_name);
    if (station == stations.end()) {
#ifdef ONEBRC_BOUNDED_MEMORY
        if (stations.size() == maximum_station_count) {
            throw std::runtime_error("station count exceeds limit of " +
                                     std::to_string(maximum_station_count));
        }
#endif
        station = stations.emplace(std::string(station_name), Stats{}).first;
    }
    station->second.add(temperature);
}
#else
void process_record(const std::string& record, StationMap& stations, std::uint64_t line_number) {
    const auto separator = record.find(';');
    if (separator == std::string::npos || separator == 0 || separator > 100 ||
        record.find(';', separator + 1) != std::string::npos) {
        throw std::runtime_error("invalid record on line " + std::to_string(line_number));
    }
    const auto temperature = parse_temperature(record.substr(separator + 1), line_number);
    stations[record.substr(0, separator)].add(temperature);
}
#endif

#if !defined(ONEBRC_BUFFERED_IO) && !defined(ONEBRC_MMAP)
void aggregate_with_getline(const std::string& input_path, StationMap& stations) {
    std::ifstream input(input_path);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + input_path);
    }

    std::string line;
    std::uint64_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
#ifndef ONEBRC_NO_ROW_ALLOCATIONS
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
#endif
        process_record(line, stations, line_number);
    }
    if (input.bad()) {
        throw std::runtime_error("failed while reading input file");
    }
}
#endif

#ifdef ONEBRC_BUFFERED_IO
void aggregate_with_buffer(const std::string& input_path, StationMap& stations) {
#ifdef ONEBRC_BUFFER_SIZE_BYTES
    constexpr std::size_t chunk_size = ONEBRC_BUFFER_SIZE_BYTES;
#else
    constexpr std::size_t chunk_size = 4U * 1024U * 1024U;
#endif
    constexpr std::size_t maximum_record_size = 108;

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + input_path);
    }

    std::vector<char> buffer(chunk_size + maximum_record_size);
    std::size_t carried = 0;
    std::uint64_t line_number = 0;
    while (true) {
        input.read(buffer.data() + carried, static_cast<std::streamsize>(chunk_size));
        const auto bytes_read = static_cast<std::size_t>(input.gcount());
        const auto available = carried + bytes_read;
        if (available == 0) {
            break;
        }

        const char* cursor = buffer.data();
        const char* const end = buffer.data() + available;
        while (cursor < end) {
            const auto remaining = static_cast<std::size_t>(end - cursor);
            const auto* newline = static_cast<const char*>(std::memchr(cursor, '\n', remaining));
            if (newline == nullptr) {
                break;
            }
            ++line_number;
            process_record(std::string_view(cursor, static_cast<std::size_t>(newline - cursor)), stations,
                           line_number);
            cursor = newline + 1;
        }

        carried = static_cast<std::size_t>(end - cursor);
        if (carried > maximum_record_size) {
            throw std::runtime_error("record exceeds maximum length near line " +
                                     std::to_string(line_number + 1));
        }
        std::memmove(buffer.data(), cursor, carried);

        if (bytes_read < chunk_size) {
            if (input.bad()) {
                throw std::runtime_error("failed while reading input file");
            }
            break;
        }
    }

    if (carried != 0) {
        ++line_number;
        process_record(std::string_view(buffer.data(), carried), stations, line_number);
    }
}
#endif

#ifdef ONEBRC_MMAP
[[nodiscard]] std::wstring utf8_to_wide(const std::string& text) {
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
    if (required == 0) {
        throw std::runtime_error("input path is not valid UTF-8");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            wide.data(), required) == 0) {
        throw std::runtime_error("failed to convert input path");
    }
    return wide;
}

class MappedFile {
public:
    explicit MappedFile(const std::string& input_path) {
        const auto wide_path = utf8_to_wide(input_path);
        file_ = CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("cannot open input file: " + input_path);
        }

        LARGE_INTEGER size{};
        if (GetFileSizeEx(file_, &size) == 0 || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
            close();
            throw std::runtime_error("cannot determine input file size");
        }
        size_ = static_cast<std::size_t>(size.QuadPart);
        if (size_ == 0) {
            return;
        }

        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            close();
            throw std::runtime_error("cannot create file mapping");
        }
        data_ = static_cast<const char*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (data_ == nullptr) {
            close();
            throw std::runtime_error("cannot map input file");
        }
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    ~MappedFile() {
        close();
    }

    [[nodiscard]] const char* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    void close() noexcept {
        if (data_ != nullptr) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_ != nullptr) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    const char* data_ = nullptr;
    std::size_t size_ = 0;
};

void aggregate_with_mapping(const std::string& input_path, StationMap& stations) {
    const MappedFile file(input_path);
    if (file.size() == 0) {
        return;
    }
    const char* cursor = file.data();
    const char* const end = cursor + file.size();
    std::uint64_t line_number = 0;
    while (cursor < end) {
        const auto remaining = static_cast<std::size_t>(end - cursor);
        const auto* newline = static_cast<const char*>(std::memchr(cursor, '\n', remaining));
        const char* const record_end = newline == nullptr ? end : newline;
        ++line_number;
        process_record(std::string_view(cursor, static_cast<std::size_t>(record_end - cursor)), stations,
                       line_number);
        cursor = newline == nullptr ? end : newline + 1;
    }
}
#endif

int run(const std::string& input_path) {
    StationMap stations;
#ifdef ONEBRC_BOUNDED_MEMORY
    stations.max_load_factor(0.8F);
    stations.reserve(maximum_station_count);
#else
    stations.reserve(512);
#endif

#ifdef ONEBRC_MMAP
    aggregate_with_mapping(input_path, stations);
#elif defined(ONEBRC_BUFFERED_IO)
    aggregate_with_buffer(input_path, stations);
#else
    aggregate_with_getline(input_path, stations);
#endif

#ifdef ONEBRC_BOUNDED_MEMORY
    std::vector<const StationMap::value_type*> sorted;
    sorted.reserve(stations.size());
    for (const auto& station : stations) {
        sorted.push_back(&station);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
        return unsigned_byte_less(left->first, right->first);
    });
#else
    std::vector<std::pair<std::string, Stats>> sorted(stations.begin(), stations.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return unsigned_byte_less(left.first, right.first);
    });
#endif

    std::cout << '{';
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
#ifdef ONEBRC_BOUNDED_MEMORY
        const auto& [name, stats] = *sorted[index];
#else
        const auto& [name, stats] = sorted[index];
#endif
        std::cout << name << '=';
        print_temperature(std::cout, stats.minimum);
        std::cout << '/';
        print_temperature(std::cout, rounded_mean_tenths(stats));
        std::cout << '/';
        print_temperature(std::cout, stats.maximum);
    }
    std::cout << "}\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "error: cannot configure stdout\n";
        return static_cast<int>(ExitCode::failure);
    }
#endif

    if (argc != 2) {
        std::cerr << "usage: onebrc_baseline <input-path>\n";
        return static_cast<int>(ExitCode::usage);
    }

    try {
        return run(argv[1]);
    }
    catch (const std::bad_alloc&) {
        std::cerr << "error: memory allocation failed\n";
        return static_cast<int>(ExitCode::allocation_failure);
    }
    catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return static_cast<int>(ExitCode::failure);
    }
}
