#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 16> stations = {
    "Abha",       "Amsterdam", "Berlin",    "Cairo",       "Cape Town", "Helsinki",
    "Hong Kong",  "London",    "Mexico City", "New York",  "Oslo",      "São Paulo",
    "St. John's", "Taipei",    "Tokyo",     "Zürich",
};

[[nodiscard]] bool parse_unsigned(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

void write_temperature(std::ostream& output, std::int32_t tenths) {
    const auto magnitude = tenths < 0 ? -tenths : tenths;
    if (tenths < 0) {
        output << '-';
    }
    output << magnitude / 10 << '.' << magnitude % 10;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        std::cerr << "usage: onebrc_generate <row-count> <output-path> [seed] [random|single|unique10000]\n";
        return 2;
    }

    std::uint64_t row_count = 0;
    std::uint64_t seed = 0x1B4C2026ULL;
    if (!parse_unsigned(argv[1], row_count) || (argc == 4 && !parse_unsigned(argv[3], seed))) {
        std::cerr << "error: row-count and seed must be unsigned integers\n";
        return 2;
    }
    if (argc == 5 && !parse_unsigned(argv[3], seed)) {
        std::cerr << "error: row-count and seed must be unsigned integers\n";
        return 2;
    }
    const std::string_view mode = argc == 5 ? argv[4] : "random";
    if (mode != "random" && mode != "single" && mode != "unique10000") {
        std::cerr << "error: mode must be random, single, or unique10000\n";
        return 2;
    }

    std::ofstream output(argv[2], std::ios::binary);
    if (!output) {
        std::cerr << "error: cannot create output file: " << argv[2] << '\n';
        return 2;
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> station_distribution(0, stations.size() - 1);
    std::uniform_int_distribution<std::int32_t> temperature_distribution(-999, 999);
    for (std::uint64_t row = 0; row < row_count; ++row) {
        if (mode == "single") {
            output << "Only Station";
        }
        else if (mode == "unique10000") {
            output << "Station" << std::setw(4) << std::setfill('0') << row % 10'000 << std::setfill(' ');
        }
        else {
            output << stations[station_distribution(random)];
        }
        output << ';';
        write_temperature(output, temperature_distribution(random));
        output << '\n';
    }

    if (!output) {
        std::cerr << "error: failed while writing output file\n";
        return 1;
    }
    return 0;
}
