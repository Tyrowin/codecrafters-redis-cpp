#include "redis/RedisServer.h"
#include "redis/Config.h"
#include "redis/Storage.h"
#include "redis/CommandHandler.h"
#include "redis/RESPParser.h"
#include "redis/RDBParser.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <algorithm>

namespace redis {

RedisServer::RedisServer(std::shared_ptr<Config> config)
    : config_(config),
      storage_(std::make_shared<Storage>()),
      commandHandler_(std::make_shared<CommandHandler>(config, storage_)),
      serverFd_(-1) {
}

RedisServer::~RedisServer() {
    for (int fd : clientFds_) {
        close(fd);
    }
    if (serverFd_ != -1) {
        close(serverFd_);
    }
}

bool RedisServer::createServerSocket() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "Failed to create server socket\n";
        return false;
    }
    
    int reuse = 1;
    if (setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return false;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config_->getPort());
    
    if (bind(serverFd_, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port " << config_->getPort() << std::endl;
        return false;
    }
    
    int connection_backlog = 5;
    if (listen(serverFd_, connection_backlog) != 0) {
        std::cerr << "listen failed" << std::endl;
        return false;
    }
    
    std::cout << "Server listening on port " << config_->getPort() << "..." << std::endl;
    return true;
}

void RedisServer::run() {
    if (!createServerSocket()) {
        return;
    }
    
    loadRDBFile();
    
    std::cout << "Logs from your program will appear here!" << std::endl;
    
    while (true) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(serverFd_, &readFds);
        
        int maxFd = serverFd_;
        for (int clientFd : clientFds_) {
            FD_SET(clientFd, &readFds);
            maxFd = std::max(maxFd, clientFd);
        }
        
        int activity = select(maxFd + 1, &readFds, nullptr, nullptr, nullptr);
        if (activity < 0) {
            std::cerr << "select error" << std::endl;
            break;
        }
        
        if (FD_ISSET(serverFd_, &readFds)) {
            handleNewConnection();
        }
        
        auto clientFdsSnapshot = clientFds_;
        for (int clientFd : clientFdsSnapshot) {
            if (FD_ISSET(clientFd, &readFds)) {
                handleClientData(clientFd);
            }
        }
    }
}

void RedisServer::handleNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int clientFd = accept(serverFd_, (struct sockaddr *) &client_addr, &client_addr_len);
    
    if (clientFd < 0) {
        std::cerr << "Failed to accept client connection" << std::endl;
        return;
    }
    
    clientFds_.insert(clientFd);
    std::cout << "New client connected (fd: " << clientFd << ")" << std::endl;
}

void RedisServer::handleClientData(int clientFd) {
    char buffer[1024];
    int bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
    
    if (bytesRead <= 0) {
        closeClient(clientFd);
        return;
    }
    
    std::string data(buffer, bytesRead);
    auto command = RESPParser::parseArray(data);
    
    if (!command.empty()) {
        std::string response = commandHandler_->handleCommand(command);
        send(clientFd, response.c_str(), response.length(), 0);
    }
}

void RedisServer::closeClient(int clientFd) {
    close(clientFd);
    clientFds_.erase(clientFd);
    std::cout << "Client disconnected (fd: " << clientFd << ")" << std::endl;
}

bool RedisServer::loadRDBFile() {
    std::string rdbPath = config_->getDir() + "/" + config_->getDbFilename();
    
    RDBParser parser;
    if (!parser.parseFile(rdbPath, *storage_)) {
        std::cerr << "Failed to parse RDB file: " << rdbPath << std::endl;
        return false;
    }
    
    return true;
}

} // namespace redis