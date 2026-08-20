#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

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
    // The canonical 1BRC rule is equivalent to Java Math.round(): floor(x + 0.5).
    const auto mean = static_cast<long double>(stats.sum) / static_cast<long double>(stats.count);
    return static_cast<std::int64_t>(std::floor(mean + 0.5L));
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

int run(const std::string& input_path) {
    std::ifstream input(input_path);
    if (!input) {
        std::cerr << "error: cannot open input file: " << input_path << '\n';
        return 2;
    }

    StationMap stations;
    stations.reserve(512);

    std::string line;
    std::uint64_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto separator = line.find(';');
        if (separator == std::string::npos || separator == 0 || separator > 100 ||
            line.find(';', separator + 1) != std::string::npos) {
            throw std::runtime_error("invalid record on line " + std::to_string(line_number));
        }

#ifdef ONEBRC_NO_ROW_ALLOCATIONS
        const std::string_view line_view(line);
        const auto temperature = parse_temperature(line_view.substr(separator + 1), line_number);
        const auto station_name = line_view.substr(0, separator);
        auto station = stations.find(station_name);
        if (station == stations.end()) {
            station = stations.emplace(std::string(station_name), Stats{}).first;
        }
        station->second.add(temperature);
#else
        const auto temperature = parse_temperature(line.substr(separator + 1), line_number);
        stations[line.substr(0, separator)].add(temperature);
#endif
    }
    if (input.bad()) {
        throw std::runtime_error("failed while reading input file");
    }

    std::vector<std::pair<std::string, Stats>> sorted(stations.begin(), stations.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        return unsigned_byte_less(left.first, right.first);
    });

    std::cout << '{';
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        const auto& [name, stats] = sorted[index];
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
        return 1;
    }
#endif

    if (argc != 2) {
        std::cerr << "usage: onebrc_baseline <input-path>\n";
        return 2;
    }

    try {
        return run(argv[1]);
    }
    catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
