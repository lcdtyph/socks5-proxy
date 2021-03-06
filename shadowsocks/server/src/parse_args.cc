#include <cstdlib>
#include <fstream>
#include <iostream>
#include <boost/program_options.hpp>

#include <crypto_utils/crypto.h>
#include <ss_proto/server.h>

#include "parse_args.h"
#include "udprelay.h"

namespace bpo = boost::program_options;
using boost::asio::ip::tcp;

void ParseArgs(int argc, char *argv[], StreamServerArgs *args, ResolverArgs *rargs,
               int *log_level, Plugin *p, UdpServerParam *udp) {
    auto factory = CryptoContextGeneratorFactory::Instance();
    bpo::options_description desc("Shadowsocks Server");
    desc.add(*GetCommonOptions()).add_options()
        ("method,m", bpo::value<std::string>(), "Cipher method")
        ("password,k", bpo::value<std::string>(), "Password")
        ("udp-relay,u", "Enable udp relay")
        ("udp-only,U", "Udp only")
        ("plugin", bpo::value<std::string>(), "Plugin executable name")
        ("plugin-opts", bpo::value<std::string>(), "Plugin options");

    bpo::variables_map vm;
    bpo::store(bpo::parse_command_line(argc, argv, desc), vm);
    bpo::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        std::vector<std::string> methods;
        factory->GetAllRegisteredNames(methods);
        std::cout << "Available methods:\n   ";
        for (auto &m : methods) {
            std::cout << " " << m;
        }
        std::cout << std::endl;
        exit(0);
    }

    if (vm.count("config-file")) {
        auto filename = vm["config-file"].as<std::string>();
        std::ifstream ifs(filename);
        if (!ifs) {
            std::cerr << "Unavailable configure file" << std::endl;
            exit(-1);
        }
        bpo::store(bpo::parse_config_file(ifs, desc), vm);
        bpo::notify(vm);
    }

    uint16_t bind_port = vm["bind-port"].as<uint16_t>();
    boost::system::error_code ec;
    auto bind_address = boost::asio::ip::make_address(vm["bind-address"].as<std::string>(), ec);
    if (ec) {
        std::cerr << "Invalid bind address" << std::endl;
        exit(-1);
    }

    if (!vm.count("password")) {
        std::cerr << "Please specify the password" << std::endl;
        exit(-1);
    }
    std::string password = vm["password"].as<std::string>();

    if (!vm.count("method")) {
        std::cerr << "Please specify a cipher method using -m option" << std::endl;
        exit(-1);
    }
    auto crypto_generator = factory->GetGenerator(vm["method"].as<std::string>(), password);
    if (!crypto_generator) {
        std::cerr << "Invalid cipher type!" << std::endl;
        exit(-1);
    }

    GetResolverArgs(vm, rargs);

    udp->udp_enable = vm.count("udp-relay");
    udp->udp_only = vm.count("udp-only");
    if (udp->udp_enable || udp->udp_only) {
        udp->bind_ep.address(bind_address);
        udp->bind_ep.port(bind_port);
        udp->crypto = (*crypto_generator)();
    }

    *log_level = vm["verbose"].as<int>();

    if (vm.count("plugin")) {
        std::string plugin = vm["plugin"].as<std::string>();
        if (!plugin.empty()) {
            p->enable = true;
            p->plugin = plugin;
            p->remote_address = vm["bind-address"].as<std::string>();
            p->remote_port = bind_port;
            p->local_address = "127.0.0.1";
            p->local_port = GetFreePort();
            bind_address = boost::asio::ip::make_address(p->local_address);
            bind_port = p->local_port;
            if (bind_port == 0) {
                std::cerr << "Fatal error: cannot get a freedom port" << std::endl;
                exit(-1);
            }
            if (vm.count("plugin-opts")) {
                p->plugin_options = vm["plugin-opts"].as<std::string>();
            }
        }
    }

    args->bind_ep = tcp::endpoint(bind_address, bind_port);
    args->timeout = vm["timeout"].as<size_t>() * 1000;

    args->generator = \
        [g = *crypto_generator]() {
            return GetProtocol<ShadowsocksServer>(g());
        };
}

