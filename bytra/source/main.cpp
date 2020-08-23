//
// Created by Arne Wouters on 29/07/2020.
//

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <cli/CLI11.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <toml++/toml.hpp>

#include "Bybit.h"
#include "TerminalColors.h"
#include "strategies/Ema.h"
#include "strategies/Rsi.h"
#include "strategies/Strategy.h"

namespace beast = boost::beast;

void signal_callback_handler(int signum) {
    std::cout << std::endl;
    std::cout << "...Terminating" << std::endl;
    spdlog::debug("Terminating program.");
    std::cout << RED << "✗ " << RESET << "Program terminated." << std::endl;
    exit(signum);
}

int main(int argc, char **argv) {
    std::ios_base::sync_with_stdio(false);
    std::cout << "    ____ __  __ ______ ____   ___ " << std::endl;
    std::cout << R"(   / __ )\ \/ //_  __// __ \ /   |)" << std::endl;
    std::cout << R"(  / __  | \  /  / /  / /_/ // /| |)" << std::endl;
    std::cout << " / /_/ /  / /  / /  / _, _// ___ |" << std::endl;
    std::cout << "/_____/  /_/  /_/  /_/ |_|/_/  |_|   Made by Arne." << std::endl << std::endl;

    std::cout << "Setting up BYTRA" << std::endl;

    // CLI setup
    CLI::App app("BYTRA");
    std::string strategy;
    CLI::Option *opt = app.add_option("-s,--strategy,strategy", strategy, "Name of the strategy")->required();
    int d{0};
    CLI::Option *flag = app.add_flag("-d,--debug", d, "Debug flag that enables debug logging");

    CLI11_PARSE(app, argc, argv);

    // Register signal and signal handler
    signal(SIGINT, signal_callback_handler);

    std::cout << " - Logging setup" << std::flush;

    // spdlog setup
    try {
        auto logger = spdlog::basic_logger_mt("basic_logger", "data/logs.txt");
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(1));
        spdlog::flush_on(spdlog::level::err);
        spdlog::set_pattern("[%Y-%m-%d %T.%f] [%L] %v");
        spdlog::info("Welcome to spdlog!");

        if (d) {
            spdlog::set_level(spdlog::level::debug);
            spdlog::debug("Debug ON");
        }

        spdlog::info("Using Strategy: {}", strategy);
    } catch (const spdlog::spdlog_ex &ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    }

    std::cout << GREEN << " ✔" << RESET << std::endl;
    std::cout << " - Parsing configuration file: " << std::flush;

    // Parsing configuration file
    toml::table tbl;
    try {
        tbl = toml::parse_file("data/configuration.toml");
    } catch (const toml::parse_error &err) {
        std::cerr << "Parsing failed:\n" << err << std::endl;
        spdlog::error("Parsing configuration failed");
        return 1;
    }

    std::map<std::string, std::shared_ptr<Strategy>> validStrategies
        = {{"rsi", std::make_shared<Rsi>()}, {"ema", std::make_shared<Ema>()}};

    if (validStrategies[strategy].get() == nullptr) {
        spdlog::error("Invalid strategy: " + strategy);
        throw std::invalid_argument("Invalid strategy: " + strategy);
    }

    std::cout << "Strategy found! " << GREEN << "✔" << RESET << std::endl;
    std::cout << " - Setting up strategy" << std::flush;

    std::string baseUrl = *tbl["bybit-testnet"]["baseUrl"].value<std::string>();
    std::string websocketHost = *tbl["bybit-testnet"]["websocketHost"].value<std::string>();
    std::string websocketTarget = *tbl["bybit-testnet"]["websocketTarget"].value<std::string>();
    std::string apiKey = *tbl["bybit-testnet"]["apiKey"].value<std::string>();
    std::string apiSecret = *tbl["bybit-testnet"]["apiSecret"].value<std::string>();

    auto bybit = std::make_shared<Bybit>(baseUrl, apiKey, apiSecret, websocketHost, websocketTarget,
                                         validStrategies[strategy]);

    std::cout << GREEN << " ✔" << RESET << std::endl;

    std::cout << "Connecting..." << std::endl;
    bybit->connect();
    int websocketHeartbeatTimer = std::time(nullptr);
    int websocketOrderBookSyncTimer = std::time(nullptr);

    // Program Loop
    for (;;) {
        while (bybit->isConnected()) {
            bybit->readWebsocket();
            bybit->doAutomatedTrading();
            bybit->removeUnusedCandles();

            int currentTime = std::time(nullptr);

            // Send heartbeat packet every 55 seconds to maintain websocket connection
            if (currentTime - websocketHeartbeatTimer > 55) {
                websocketHeartbeatTimer = currentTime;
                bybit->sendWebsocketHeartbeat();
            }

            // Sync order book after an hour
            if (currentTime - websocketOrderBookSyncTimer > 3600) {
                websocketOrderBookSyncTimer = currentTime;
                bybit->syncOrderBook();
            }
        }
        sleep(3);
        std::cout << "Attempting to reconnect..." << std::endl;
        bybit->connect();
    }

    return 0;
}
