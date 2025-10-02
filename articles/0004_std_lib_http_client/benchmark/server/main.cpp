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
#include <boost/beast/core/multi_buffer.hpp>

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
    bool verify = false;
};

struct ResponseCache {
    std::string data_block;
    std::vector<boost::beast::string_view> body_views;
    std::vector<boost::beast::http::response<boost::beast::http::empty_body>> header_templates;
};

bool parse_args(int argc, char* argv[], Config& config) {
    try {
        po::options_description desc("Benchmark Server Options");
        desc.add_options()
            ("help,h", "Show this help message")
            ("transport", po::value<std::string>(&config.transport_type)->default_value("tcp"), "Transport to use: 'tcp' or 'unix'")
            ("seed", po::value<uint32_t>(&config.seed)->default_value(1234), "Seed for the PRNG")
            ("verify", po::value<bool>(&config.verify)->default_value(true), "Include checksum calculations")
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

uint64_t xor_checksum(boost::beast::string_view body) {
    return std::accumulate(body.begin(), body.end(), std::uint64_t{0}, std::bit_xor<>());
}


std::string get_timestamp_str() {
    auto const now = std::chrono::high_resolution_clock::now();
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return std::to_string(ns);
}

ResponseCache generate_responses(const Config& config) {
    ResponseCache cache;

    std::mt19937 gen(config.seed);

    constexpr size_t CHECKSUM_LEN = 16;
    constexpr size_t TIMESTAMP_LEN = 19;
    constexpr size_t METADATA_LEN = CHECKSUM_LEN + TIMESTAMP_LEN;

    if (config.min_length > config.max_length) {
        std::cerr << "Error: --min-length cannot be greater than --max-length." << std::endl;
        return {};
    }

    cache.data_block.resize(config.max_length);
    std::uniform_int_distribution<char> char_dist(32, 126);
    for (char& c : cache.data_block) {
        c = char_dist(gen);
    }

    std::uniform_int_distribution<size_t> len_dist(config.min_length, config.max_length - METADATA_LEN);

    cache.body_views.reserve(config.num_responses);
    cache.header_templates.reserve(config.num_responses);

    for (int i = 0; i < config.num_responses; ++i) {
        size_t body_len = len_dist(gen);

        std::uniform_int_distribution<size_t> offset_dist(0, config.max_length - body_len);
        size_t start_offset = offset_dist(gen);

        boost::beast::string_view body_view(&cache.data_block[start_offset], body_len);
        cache.body_views.push_back(body_view);

        boost::beast::http::response<boost::beast::http::empty_body> header_template;
        header_template.version(11);
        header_template.result(boost::beast::http::status::ok);
        header_template.set(boost::beast::http::field::server, "BenchmarkServer");
        header_template.set(boost::beast::http::field::content_type, "text/plain");
        header_template.set(boost::beast::http::field::content_length, std::to_string(body_len + METADATA_LEN));
        cache.header_templates.push_back(header_template);
    }

    std::cout << "Generated " << config.num_responses << " response views into a single data block." << std::endl;
    return cache;
}

template<class Stream>
void do_session(Stream& stream, const ResponseCache& cache, Config& config) {
    namespace http = boost::beast::http;

    size_t response_index = 0;
    boost::beast::flat_buffer buffer;
    boost::system::error_code ec;

    for (;;) {
        http::request<http::string_body> req;
        http::read(stream, buffer, req, ec);

        if (ec == http::error::end_of_stream) { break; }
        if (ec) { std::cerr << "Session read error: " << ec.message() << std::endl; break; }

        constexpr size_t REQ_CHECKSUM_LEN = 16;
        if (config.verify && req.body().size() >= REQ_CHECKSUM_LEN) {
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

        const auto& header_template = cache.header_templates[response_index];
        const auto& body_view = cache.body_views[response_index];

        std::string ts = get_timestamp_str();

        uint64_t checksum_val = xor_checksum(body_view);
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << checksum_val;
        std::string checksum_str = ss.str();

        http::response<http::string_body> res;
        res.base() = header_template;

        res.body().reserve(body_view.size() + checksum_str.size() + ts.size());
        res.body().append(body_view.data(), body_view.size());
        res.body().append(checksum_str);
        res.body().append(ts);

        http::write(stream, res, ec);

        response_index = (response_index + 1) % cache.body_views.size();

        if (ec) { std::cerr << "Session write error: " << ec.message() << std::endl; break; }
        if (!req.keep_alive()) { break; }
    }

    if constexpr (std::is_same_v<typename Stream::protocol_type, boost::asio::ip::tcp>) {
        stream.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
    } else {
        stream.shutdown(boost::asio::local::stream_protocol::socket::shutdown_send, ec);
    }
}

template<class Acceptor, class Endpoint>
void do_listen(boost::asio::io_context& ioc, const Endpoint& endpoint, const ResponseCache& cache, Config& config) {
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

    do_session(socket, cache, config);

    std::cout << "Session complete. Server shutting down." << std::endl;
}

int main(int argc, char* argv[]) {
    Config config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }


    auto response_cache = generate_responses(config);
    if (response_cache.body_views.empty()) {
        return 1;
    }

    boost::asio::io_context ioc;

    if (config.transport_type == "tcp") {
        auto const endpoint = boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(config.host), config.port};
        do_listen<boost::asio::ip::tcp::acceptor, boost::asio::ip::tcp::endpoint>(ioc, endpoint, response_cache, config);
    } else if (config.transport_type == "unix") {
        std::remove(config.unix_socket_path.c_str());
        auto const endpoint = boost::asio::local::stream_protocol::endpoint{config.unix_socket_path};
        do_listen<boost::asio::local::stream_protocol::acceptor, boost::asio::local::stream_protocol::endpoint>(ioc, endpoint, response_cache, config);
    }

    return 0;
}