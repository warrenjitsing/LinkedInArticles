#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>
#include <memory>
#include <fstream>
#include <iomanip>
#include <type_traits>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

struct Config {
    std::string transport_type = "tcp";
    uint32_t seed = 1234;
    int num_responses = 100;
    size_t min_length = 1024;
    size_t max_length = 1024 * 1024;
    std::string host = "127.0.0.1";
    unsigned short port = 8080;
    std::string unix_socket_path = "/tmp/httpc_benchmark.sock";
};

bool parse_args(int argc, char* argv[], Config& config) {
    try {
        po::options_description desc("Benchmark Server Options");
        desc.add_options()
            ("help,h", "Show this help message")
            ("transport", po::value<std::string>(&config.transport_type)->default_value("tcp"), "Transport to use: 'tcp' or 'unix'")
            ("seed", po::value<uint32_t>(&config.seed)->default_value(1234), "Seed for the PRNG")
            ("num-responses", po::value<int>(&config.num_responses)->default_value(100), "Number of response templates to generate")
            ("min-length", po::value<size_t>(&config.min_length)->default_value(1024), "Minimum response body size in bytes")
            ("max-length", po::value<size_t>(&config.max_length)->default_value(1024 * 1024), "Maximum response body size in bytes")
            ("host", po::value<std::string>(&config.host)->default_value("127.0.0.1"), "Host to bind for TCP transport")
            ("port", po::value<unsigned short>(&config.port)->default_value(8080), "Port to bind for TCP transport")
            ("unix-socket-path", po::value<std::string>(&config.unix_socket_path)->default_value("/tmp/httpc_benchmark.sock"), "Path for the Unix domain socket")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return false;
        }

        if (config.transport_type != "tcp" && config.transport_type != "unix") {
            std::cerr << "Error: --transport must be either 'tcp' or 'unix'." << std::endl;
            return false;
        }

    } catch (const po::error& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool oom_check(const Config& config) {
    size_t required_memory = static_cast<size_t>(config.num_responses) * config.max_length;
    long long available_memory_kb = 0;

    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        std::cerr << "Warning: Could not open /proc/meminfo to check available memory." << std::endl;
        return true;
    }

    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            try {
                available_memory_kb = std::stoll(line.substr(line.find(':') + 1));
            } catch (...) {
                std::cerr << "Warning: Could not parse MemAvailable from /proc/meminfo." << std::endl;
                return true;
            }
            break;
        }
    }

    if (available_memory_kb == 0) {
        std::cerr << "Warning: Could not determine available memory." << std::endl;
        return true; // Fail open
    }

    size_t available_memory = available_memory_kb * 1024;

    if (required_memory > available_memory * 0.9) {
        std::cerr << "Error: Not enough memory." << std::endl;
        std::cerr << "  Required for response cache: " << required_memory / (1024 * 1024) << " MB" << std::endl;
        std::cerr << "  Available on system: " << available_memory / (1024 * 1024) << " MB" << std::endl;
        std::cerr << "Please reduce --num-responses or --max-length." << std::endl;
        return false;
    }
    return true;
}

uint64_t xor_checksum(boost::beast::string_view body) {
    return std::accumulate(body.begin(), body.end(), std::uint64_t{0}, std::bit_xor<>());
}


std::string get_timestamp_str() {
    auto const now = std::chrono::high_resolution_clock::now();
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return std::to_string(ns);
}

std::vector<boost::beast::http::response<boost::beast::http::string_body>>
generate_responses(const Config& config) {
    std::vector<boost::beast::http::response<boost::beast::http::string_body>> responses;
    responses.reserve(config.num_responses);

    std::mt19937 gen(config.seed);

    constexpr size_t CHECKSUM_LEN = 16;
    constexpr size_t TIMESTAMP_LEN = 19;
    constexpr size_t METADATA_LEN = CHECKSUM_LEN + TIMESTAMP_LEN;

    if (config.max_length <= METADATA_LEN) {
        std::cerr << "Error: --max-length must be greater than " << METADATA_LEN << std::endl;
        return {};
    }

    if (config.min_length > config.max_length - METADATA_LEN) {
        std::cerr << "Error: --min-length (" << config.min_length
                  << ") cannot be greater than effective max length ("
                  << config.max_length - METADATA_LEN << ")." << std::endl;
        return {};
    }


    std::uniform_int_distribution<size_t> len_dist(config.min_length, config.max_length - METADATA_LEN);
    std::uniform_int_distribution<char> char_dist(32, 126);

    for (int i = 0; i < config.num_responses; ++i) {
        size_t body_len = len_dist(gen);
        std::string random_body(body_len, '\0');
        for (char& c : random_body) {
            c = char_dist(gen);
        }

        uint64_t checksum_val = xor_checksum(random_body);
        std::stringstream ss;
        ss << std::hex << std::setw(CHECKSUM_LEN) << std::setfill('0') << checksum_val;

        std::string final_body = random_body + ss.str() + std::string(TIMESTAMP_LEN, '0');

        boost::beast::http::response<boost::beast::http::string_body> res;
        res.version(11); // HTTP/1.1
        res.result(boost::beast::http::status::ok);
        res.set(boost::beast::http::field::server, "BenchmarkServer");
        res.set(boost::beast::http::field::content_type, "text/plain");
        res.body() = final_body;
        res.prepare_payload();

        responses.push_back(std::move(res));
    }

    std::cout << "Generated " << responses.size() << " response templates." << std::endl;
    return responses;
}

template<class Stream>
void do_session(Stream& stream, const std::vector<boost::beast::http::response<boost::beast::http::string_body>>& responses) {
    namespace http = boost::beast::http;

    size_t response_index = 0;
    boost::beast::flat_buffer buffer;
    boost::system::error_code ec;

    for (;;) {
        http::request<http::string_body> req;
        http::read(stream, buffer, req, ec);

        if (ec == http::error::end_of_stream) {
            break;
        }
        if (ec) {
            std::cerr << "Session read error: " << ec.message() << std::endl;
            break;
        }

        constexpr size_t REQ_CHECKSUM_LEN = 16;
        if (req.body().size() >= REQ_CHECKSUM_LEN) {
            auto payload_view = boost::beast::string_view(req.body()).substr(0, req.body().size() - REQ_CHECKSUM_LEN);
            auto received_checksum_hex = boost::beast::string_view(req.body()).substr(req.body().size() - REQ_CHECKSUM_LEN);

            uint64_t calculated_checksum = xor_checksum(payload_view);

            uint64_t received_checksum = 0;
            std::stringstream ss;
            ss << std::hex << std::string(received_checksum_hex);
            ss >> received_checksum;

            if (calculated_checksum != received_checksum) {
                std::cerr << "Warning: Checksum mismatch from client!" << std::endl;
            }
        }

        http::response<http::string_body> res = responses[response_index];
        response_index = (response_index + 1) % responses.size();

        constexpr size_t TIMESTAMP_LEN = 19;
        std::string ts = get_timestamp_str();
        if (ts.length() > TIMESTAMP_LEN) {
            ts.resize(TIMESTAMP_LEN);
        }
        memcpy(&res.body()[res.body().size() - TIMESTAMP_LEN], ts.c_str(), ts.length());

        http::write(stream, res, ec);
        if (ec) {
            std::cerr << "Session write error: " << ec.message() << std::endl;
            break;
        }

        if (!req.keep_alive()) {
            break;
        }
    }

    if constexpr (std::is_same_v<typename Stream::protocol_type, boost::asio::ip::tcp>) {
        stream.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
    } else {
        stream.shutdown(boost::asio::local::stream_protocol::socket::shutdown_send, ec);
    }
}

template<class Acceptor, class Endpoint>
void do_listen(boost::asio::io_context& ioc, const Endpoint& endpoint, const std::vector<boost::beast::http::response<boost::beast::http::string_body>>& responses) {
    boost::system::error_code ec;

    Acceptor acceptor(ioc);

    acceptor.open(endpoint.protocol(), ec);
    if(ec) {
        std::cerr << "Failed to open acceptor: " << ec.message() << std::endl;
        return;
    }

    acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if(ec) {
        std::cerr << "Failed to set reuse_address: " << ec.message() << std::endl;
        return;
    }

    acceptor.bind(endpoint, ec);
    if(ec) {
        std::cerr << "Failed to bind to endpoint: " << ec.message() << std::endl;
        return;
    }

    acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if(ec) {
        std::cerr << "Failed to listen on endpoint: " << ec.message() << std::endl;
        return;
    }

    std::cout << "Server listening for a single connection..." << std::endl;

    auto socket = acceptor.accept(ioc, ec);
    if(ec) {
        std::cerr << "Failed to accept connection: " << ec.message() << std::endl;
        return;
    }

    do_session(socket, responses);

    std::cout << "Session complete. Server shutting down." << std::endl;
}


int main(int argc, char* argv[]) {
    Config config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }

    if (!oom_check(config)) {
        return 1;
    }

    auto responses = generate_responses(config);
    if (responses.empty()) {
        return 1;
    }

    boost::asio::io_context ioc;

    if (config.transport_type == "tcp") {
        auto const endpoint = boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(config.host), config.port};
        do_listen<boost::asio::ip::tcp::acceptor, boost::asio::ip::tcp::endpoint>(ioc, endpoint, responses);
    } else if (config.transport_type == "unix") {
        std::remove(config.unix_socket_path.c_str());
        auto const endpoint = boost::asio::local::stream_protocol::endpoint{config.unix_socket_path};
        do_listen<boost::asio::local::stream_protocol::acceptor, boost::asio::local::stream_protocol::endpoint>(ioc, endpoint, responses);
    }

    return 0;
}