#include <cstdlib>
#include <iostream>
#include <sstream>
#include <boost/program_options.hpp>

#include <obfs_utils/obfs.h>
#include <obfs_utils/obfs_proto.h>

#include "parse_args.h"

namespace bpo = boost::program_options;

auto ParseArgs(int argc, char *argv[], uint16_t *bind_port, int *log_level)
    -> std::function<std::unique_ptr<BasicProtocol>(void)> {
    auto factory = ObfsGeneratorFactory::Instance();
    bpo::options_description desc("Simple Obfs Client");
    desc.add_options()
        ("bind-port,l", bpo::value<uint16_t>(),
            "Specific port that server will listen")
        ("server-address,s", bpo::value<std::string>(), "Server address")
        ("server-port,p", bpo::value<uint16_t>(), "Server port")
        ("obfs", bpo::value<std::string>(), "Obfuscate mode")
        ("obfs-host", bpo::value<std::string>(), "Obfuscate hostname")
        ("verbose", bpo::value<int>()->default_value(1),"Verbose log")
        ("help,h", "Print this help message");

    bpo::variables_map vm;
    bpo::store(bpo::parse_command_line(argc, argv, desc), vm);
    bpo::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        std::vector<std::string> modes;
        factory->GetAllRegisteredNames(modes);
        std::cout << "Available obfs modes:\n   ";
        for (auto &m : modes) {
            std::cout << " " << m;
        }
        std::cout << std::endl;
        exit(0);
    }

    bool standalone_mode = false;
    char *opts_from_env = getenv("SS_PLUGIN_OPTIONS");
    if (opts_from_env) {
        std::string opts = opts_from_env;
        std::replace(opts.begin(), opts.end(), ';', '\n');
        std::istringstream iss(opts);

        bpo::store(bpo::parse_config_file(iss, desc), vm);
        bpo::notify(vm);
        standalone_mode = true;
    }

    *log_level = vm["verbose"].as<int>();

    if (!vm.count("obfs") || !vm.count("obfs-host")) {
        std::cerr << "Please specify obfs options using --obfs & --obfs-host options"
                  << std::endl;
        exit(-1);
    }

    std::string obfs_host = vm["obfs-host"].as<std::string>();

    auto ObfsGenerator = factory->GetGenerator(vm["obfs"].as<std::string>(), obfs_host);
    if (!ObfsGenerator) {
        std::cerr << "Invalid obfs mode!" << std::endl;
        exit(-1);
    }

    std::string server_host;
    uint16_t server_port;
    if (standalone_mode) {
        char *opt_from_env;
        if (!(opt_from_env = getenv("SS_LOCAL_PORT"))) {
            exit(-1);
        }
        *bind_port = (uint16_t)std::stoul(opt_from_env);

        if (!(opt_from_env = getenv("SS_REMOTE_HOST"))) {
            exit(-1);
        }
        server_host = opt_from_env;

        if (!(opt_from_env = getenv("SS_REMOTE_PORT"))) {
            exit(-1);
        }
        server_port = (uint16_t)std::stoul(opt_from_env);
    } else {
        if (!vm.count("bind-port")) {
            std::cerr << "Please specify the bind port" << std::endl;
            exit(-1);
        }
        *bind_port = vm["bind-port"].as<uint16_t>();

        if (!vm.count("server-address")) {
            std::cerr << "Please specify the server address" << std::endl;
            exit(-1);
        }
        server_host = vm["server-address"].as<std::string>();

        if (!vm.count("server-port")) {
            std::cerr << "Please specify the server port" << std::endl;
            exit(-1);
        }
        server_port = vm["server-port"].as<uint16_t>();
    }

    boost::system::error_code ec;
    auto server_address = boost::asio::ip::make_address(server_host, ec);

    if (!ec) {
        boost::asio::ip::tcp::endpoint ep(server_address, server_port);
        return [ep, g = std::move(ObfsGenerator)]() {
                   return GetProtocol<ObfsClient>(ep, (*g)());
               };
    }
    return [s = std::move(server_host),
            p = server_port,
            g = std::move(ObfsGenerator)]() {
               return GetProtocol<ObfsClient>(s, p, (*g)());
           };
}

