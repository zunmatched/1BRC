#include <algorithm>
#include <array>
#include <charconv>
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
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#if defined(ONEBRC_MMAP) || defined(ONEBRC_PARALLEL)
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

    void merge(const Stats& other) {
        minimum = std::min(minimum, other.minimum);
        maximum = std::max(maximum, other.maximum);
        sum += other.sum;
        count += other.count;
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

#ifdef ONEBRC_FLAT_STATION_MAP
class FlatStationMap {
public:
    using value_type = std::pair<std::string, Stats>;

private:
    static constexpr std::size_t slot_count = 16'384;
    static_assert((slot_count & (slot_count - 1)) == 0);
    static_assert(maximum_station_count < slot_count);

    template <typename UnderlyingIterator>
    class BasicIterator {
    public:
        BasicIterator(UnderlyingIterator current, UnderlyingIterator end, bool skip_empty_slots = true)
            : current_(current), end_(end) {
            if (skip_empty_slots) {
                skip_empty();
            }
        }

        decltype(auto) operator*() const { return *current_; }
        auto operator->() const { return &*current_; }

        BasicIterator& operator++() {
            ++current_;
            skip_empty();
            return *this;
        }

        friend bool operator==(const BasicIterator&, const BasicIterator&) = default;

    private:
        void skip_empty() {
            while (current_ != end_ && current_->first.empty()) {
                ++current_;
            }
        }

        UnderlyingIterator current_;
        UnderlyingIterator end_;
    };

public:
    using iterator = BasicIterator<std::vector<value_type>::iterator>;
    using const_iterator = BasicIterator<std::vector<value_type>::const_iterator>;

    FlatStationMap() : slots_(slot_count) {}

    void max_load_factor(float) noexcept {}
    void reserve(std::size_t) noexcept {}
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] iterator begin() { return iterator(slots_.begin(), slots_.end()); }
    [[nodiscard]] iterator end() { return iterator(slots_.end(), slots_.end()); }
    [[nodiscard]] const_iterator begin() const { return const_iterator(slots_.begin(), slots_.end()); }
    [[nodiscard]] const_iterator end() const { return const_iterator(slots_.end(), slots_.end()); }

    [[nodiscard]] iterator find(std::string_view name) {
        const auto slot = find_slot(name);
        return slot->first.empty() ? end() : iterator(slot, slots_.end(), false);
    }

    [[nodiscard]] const_iterator find(std::string_view name) const {
        const auto slot = find_slot(name);
        return slot->first.empty() ? end() : const_iterator(slot, slots_.end(), false);
    }

    std::pair<iterator, bool> emplace(std::string name, Stats stats) {
        auto slot = find_slot(name);
        if (slot->first.empty()) {
            *slot = value_type(std::move(name), stats);
            ++size_;
            return {iterator(slot, slots_.end(), false), true};
        }
        return {iterator(slot, slots_.end(), false), false};
    }

private:
    [[nodiscard]] static std::size_t hash(std::string_view value) noexcept {
        std::uint64_t result = 14'695'981'039'346'656'037ULL;
        for (const unsigned char character : value) {
            result ^= character;
            result *= 1'099'511'628'211ULL;
        }
        return static_cast<std::size_t>(result);
    }

    [[nodiscard]] std::vector<value_type>::iterator find_slot(std::string_view name) {
        auto index = hash(name) & (slot_count - 1);
        while (!slots_[index].first.empty() && slots_[index].first != name) {
            index = (index + 1) & (slot_count - 1);
        }
        return slots_.begin() + static_cast<std::ptrdiff_t>(index);
    }

    [[nodiscard]] std::vector<value_type>::const_iterator find_slot(std::string_view name) const {
        auto index = hash(name) & (slot_count - 1);
        while (!slots_[index].first.empty() && slots_[index].first != name) {
            index = (index + 1) & (slot_count - 1);
        }
        return slots_.begin() + static_cast<std::ptrdiff_t>(index);
    }

    std::vector<value_type> slots_;
    std::size_t size_ = 0;
};

using StationMap = FlatStationMap;
#else
using StationMap = std::unordered_map<std::string, Stats, TransparentStringHash, TransparentStringEqual>;
#endif
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

#if !defined(ONEBRC_BUFFERED_IO) && !defined(ONEBRC_MMAP) && !defined(ONEBRC_PARALLEL)
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

#if defined(ONEBRC_MMAP) || defined(ONEBRC_PARALLEL)
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

#ifdef ONEBRC_MMAP
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

#ifdef ONEBRC_PARALLEL
constexpr std::size_t maximum_thread_count = 32;

[[nodiscard]] std::size_t default_thread_count() noexcept {
    const auto detected = static_cast<std::size_t>(std::thread::hardware_concurrency());
    return std::clamp(detected, std::size_t{1}, maximum_thread_count);
}

[[nodiscard]] std::size_t parse_thread_count(std::string_view text) {
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || result == 0 ||
        result > maximum_thread_count) {
        throw std::runtime_error("thread count must be between 1 and " +
                                 std::to_string(maximum_thread_count));
    }
    return result;
}

void merge_stations(StationMap& destination, const StationMap& source) {
    for (const auto& [name, stats] : source) {
        auto station = destination.find(name);
        if (station == destination.end()) {
            if (destination.size() == maximum_station_count) {
                throw std::runtime_error("station count exceeds limit of " +
                                         std::to_string(maximum_station_count));
            }
            station = destination.emplace(name, Stats{}).first;
        }
        station->second.merge(stats);
    }
}

void aggregate_buffered_range(const std::string& input_path, std::size_t begin, std::size_t end,
                              StationMap& stations) {
    constexpr std::size_t chunk_size = 256U * 1024U;
    constexpr std::size_t maximum_record_size = 108;

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open input file: " + input_path);
    }
    input.seekg(static_cast<std::streamoff>(begin));
    if (!input) {
        throw std::runtime_error("cannot seek within input file");
    }

    std::vector<char> buffer(chunk_size + maximum_record_size);
    auto remaining_in_range = end - begin;
    std::size_t carried = 0;
    std::uint64_t local_line_number = 0;
    while (remaining_in_range != 0) {
        const auto requested = std::min(chunk_size, remaining_in_range);
        input.read(buffer.data() + carried, static_cast<std::streamsize>(requested));
        const auto bytes_read = static_cast<std::size_t>(input.gcount());
        if (bytes_read != requested) {
            throw std::runtime_error("failed while reading input file");
        }
        remaining_in_range -= bytes_read;

        const char* cursor = buffer.data();
        const char* const buffer_end = buffer.data() + carried + bytes_read;
        while (cursor < buffer_end) {
            const auto available = static_cast<std::size_t>(buffer_end - cursor);
            const auto* newline = static_cast<const char*>(std::memchr(cursor, '\n', available));
            if (newline == nullptr) {
                break;
            }
            ++local_line_number;
            process_record(std::string_view(cursor, static_cast<std::size_t>(newline - cursor)), stations,
                           local_line_number);
            cursor = newline + 1;
        }

        carried = static_cast<std::size_t>(buffer_end - cursor);
        if (carried > maximum_record_size) {
            throw std::runtime_error("record exceeds maximum length in input range");
        }
        std::memmove(buffer.data(), cursor, carried);
    }

    if (carried != 0) {
        ++local_line_number;
        process_record(std::string_view(buffer.data(), carried), stations, local_line_number);
    }
}

void aggregate_in_parallel(const std::string& input_path, StationMap& stations,
                           std::size_t requested_threads) {
    constexpr std::size_t maximum_record_size = 108;
    std::ifstream boundary_input(input_path, std::ios::binary | std::ios::ate);
    if (!boundary_input) {
        throw std::runtime_error("cannot open input file: " + input_path);
    }
    const auto stream_size = static_cast<std::streamoff>(boundary_input.tellg());
    if (stream_size < 0 || static_cast<std::uint64_t>(stream_size) >
                               static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("cannot determine input file size");
    }
    const auto file_size = static_cast<std::size_t>(stream_size);
    if (file_size == 0) {
        return;
    }

    const auto requested_range_count = std::min(requested_threads, file_size);
    std::vector<std::size_t> boundaries(requested_range_count + 1, file_size);
    boundaries.front() = 0;
    std::array<char, maximum_record_size + 1> boundary_buffer{};
    for (std::size_t index = 1; index < requested_range_count; ++index) {
        const auto nominal = file_size / requested_range_count * index;
        boundary_input.clear();
        boundary_input.seekg(static_cast<std::streamoff>(nominal));
        const auto bytes_to_scan = std::min(boundary_buffer.size(), file_size - nominal);
        boundary_input.read(boundary_buffer.data(), static_cast<std::streamsize>(bytes_to_scan));
        const auto bytes_read = static_cast<std::size_t>(boundary_input.gcount());
        const auto* newline = static_cast<const char*>(
            std::memchr(boundary_buffer.data(), '\n', bytes_read));
        if (newline == nullptr) {
            if (nominal + bytes_read != file_size) {
                throw std::runtime_error("record exceeds maximum length near partition boundary");
            }
            boundaries[index] = file_size;
        }
        else {
            boundaries[index] = nominal +
                                static_cast<std::size_t>(newline + 1 - boundary_buffer.data());
        }
    }

    const auto thread_count = boundaries.size() - 1;
    std::vector<StationMap> local_stations(thread_count);
    std::vector<std::exception_ptr> failures(thread_count);
    std::vector<std::jthread> workers;
    workers.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
        local_stations[index].max_load_factor(0.8F);
        local_stations[index].reserve(maximum_station_count);
        workers.emplace_back([&, index]() {
            try {
                aggregate_buffered_range(input_path, boundaries[index], boundaries[index + 1],
                                         local_stations[index]);
            }
            catch (...) {
                failures[index] = std::current_exception();
            }
        });
    }
    workers.clear();

    for (const auto& failure : failures) {
        if (failure != nullptr) {
            std::rethrow_exception(failure);
        }
    }
    for (const auto& local : local_stations) {
        merge_stations(stations, local);
    }
}
#endif
#endif

int run(const std::string& input_path
#ifdef ONEBRC_PARALLEL
        , std::size_t thread_count
#endif
) {
    StationMap stations;
#ifdef ONEBRC_BOUNDED_MEMORY
    stations.max_load_factor(0.8F);
    stations.reserve(maximum_station_count);
#else
    stations.reserve(512);
#endif

#ifdef ONEBRC_PARALLEL
    aggregate_in_parallel(input_path, stations, thread_count);
#elif defined(ONEBRC_MMAP)
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

#ifdef ONEBRC_PARALLEL
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: onebrc_parallel <input-path> [thread-count]\n";
        return static_cast<int>(ExitCode::usage);
    }
#else
    if (argc != 2) {
        std::cerr << "usage: onebrc_baseline <input-path>\n";
        return static_cast<int>(ExitCode::usage);
    }
#endif

    try {
#ifdef ONEBRC_PARALLEL
        const auto thread_count = argc == 3 ? parse_thread_count(argv[2]) : default_thread_count();
        return run(argv[1], thread_count);
#else
        return run(argv[1]);
#endif
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
