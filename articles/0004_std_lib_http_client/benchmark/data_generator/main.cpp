#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <cstdint>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

struct Config {
    uint32_t seed = 1234;
    uint64_t num_requests = 1000;
    size_t min_length = 64;
    size_t max_length = 1024;
    std::string output_file = "benchmark_data.bin";
};

bool parse_args(int argc, char* argv[], Config& config) {
    try {
        po::options_description desc("Benchmark Data Generator Options");
        desc.add_options()
            ("help,h", "Show this help message")
            ("seed", po::value<uint32_t>(&config.seed)->default_value(1234), "Seed for the PRNG")
            ("num-requests", po::value<uint64_t>(&config.num_requests)->default_value(1000), "Number of request sizes to generate")
            ("min-length", po::value<size_t>(&config.min_length)->default_value(64), "Minimum request body size in bytes")
            ("max-length", po::value<size_t>(&config.max_length)->default_value(1024), "Maximum request body size and size of the data block")
            ("output,o", po::value<std::string>(&config.output_file)->default_value("benchmark_data.bin"), "Output file name");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return false;
        }
        if (config.min_length > config.max_length) {
            std::cerr << "Error: --min-length cannot be greater than --max-length." << std::endl;
            return false;
        }
    } catch (const po::error& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    Config config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }

    std::ofstream out_file(config.output_file, std::ios::binary);
    if (!out_file) {
        std::cerr << "Error: Could not open output file " << config.output_file << std::endl;
        return 1;
    }

    std::mt19937 gen(config.seed);
    std::uniform_int_distribution<size_t> len_dist(config.min_length, config.max_length);
    std::uniform_int_distribution<unsigned char> char_dist(32, 126);

    // 1. Write the number of requests
    out_file.write(reinterpret_cast<const char*>(&config.num_requests), sizeof(config.num_requests));

    // 2. Generate and write the list of sizes
    std::vector<uint64_t> sizes;
    sizes.reserve(config.num_requests);
    for (uint64_t i = 0; i < config.num_requests; ++i) {
        sizes.push_back(len_dist(gen));
    }
    out_file.write(reinterpret_cast<const char*>(sizes.data()), sizes.size() * sizeof(uint64_t));

    // 3. Generate and write the single large data block
    std::vector<char> data_block(config.max_length);
    for (char& c : data_block) {
        c = static_cast<char>(char_dist(gen));
    }
    out_file.write(data_block.data(), data_block.size());

    std::cout << "Successfully wrote benchmark data to " << config.output_file << std::endl;
    std::cout << "  - Number of requests: " << config.num_requests << std::endl;
    std::cout << "  - Data block size: " << config.max_length / 1024.0 << " KB" << std::endl;

    return 0;
}