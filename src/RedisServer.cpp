#include "redis/RedisServer.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "redis/CommandHandler.h"
#include "redis/Config.h"
#include "redis/RDBParser.h"
#include "redis/RESPParser.h"
#include "redis/Storage.h"

namespace redis {

RedisServer::RedisServer(std::shared_ptr<Config> config)
    : config_(config),
      storage_(std::make_shared<Storage>()),
      commandHandler_(std::make_shared<CommandHandler>(config, storage_)),
      serverFd_(-1),
      masterFd_(-1) {}

RedisServer::~RedisServer() {
  for (int fd : clientFds_) {
    close(fd);
  }
  if (serverFd_ != -1) {
    close(serverFd_);
  }
  if (masterFd_ != -1) {
    close(masterFd_);
  }
}

bool RedisServer::createServerSocket() {
  serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (serverFd_ < 0) {
    std::cerr << "Failed to create server socket\n";
    return false;
  }

  int reuse = 1;
  if (setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return false;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(config_->getPort());

  if (bind(serverFd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port " << config_->getPort() << std::endl;
    return false;
  }

  int connection_backlog = 5;
  if (listen(serverFd_, connection_backlog) != 0) {
    std::cerr << "listen failed" << std::endl;
    return false;
  }

  std::cout << "Server listening on port " << config_->getPort() << "..."
            << std::endl;
  return true;
}

bool RedisServer::connectToMaster() {
  masterFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (masterFd_ < 0) {
    std::cerr << "Failed to create socket for master connection\n";
    return false;
  }

  struct sockaddr_in master_addr;
  master_addr.sin_family = AF_INET;
  master_addr.sin_port = htons(config_->getMasterPort());

  // Try to parse as IP address first
  if (inet_pton(AF_INET, config_->getMasterHost().c_str(),
                &master_addr.sin_addr) <= 0) {
    // If not an IP, try to resolve hostname
    struct hostent *host = gethostbyname(config_->getMasterHost().c_str());
    if (host == nullptr) {
      std::cerr << "Failed to resolve master hostname: "
                << config_->getMasterHost() << "\n";
      close(masterFd_);
      masterFd_ = -1;
      return false;
    }
    memcpy(&master_addr.sin_addr, host->h_addr, host->h_length);
  }

  if (connect(masterFd_, (struct sockaddr *)&master_addr, sizeof(master_addr)) <
      0) {
    std::cerr << "Failed to connect to master at " << config_->getMasterHost()
              << ":" << config_->getMasterPort() << "\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }

  std::cout << "Connected to master at " << config_->getMasterHost() << ":"
            << config_->getMasterPort() << "\n";

  // Send PING command as part of the handshake
  std::string pingCommand = RESPParser::encodeArray({"PING"});
  if (send(masterFd_, pingCommand.c_str(), pingCommand.length(), 0) < 0) {
    std::cerr << "Failed to send PING to master\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }

  // Receive PONG response
  char buffer[256];
  int bytesRead = recv(masterFd_, buffer, sizeof(buffer), 0);
  if (bytesRead <= 0) {
    std::cerr << "Failed to receive PONG from master\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::string pongResponse(buffer, bytesRead);
  std::string parsedResponse = RESPParser::parseSimpleString(pongResponse);
  if (parsedResponse != "PONG") {
    std::cerr << "Unexpected response to PING: " << parsedResponse << "\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::cout << "Received PONG from master\n";
  
  // Send REPLCONF listening-port
  std::string replconfPort = RESPParser::encodeArray({"REPLCONF", "listening-port", std::to_string(config_->getPort())});
  if (send(masterFd_, replconfPort.c_str(), replconfPort.length(), 0) < 0) {
    std::cerr << "Failed to send REPLCONF listening-port to master\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  // Receive OK response
  bytesRead = recv(masterFd_, buffer, sizeof(buffer), 0);
  if (bytesRead <= 0) {
    std::cerr << "Failed to receive response to REPLCONF listening-port\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::string okResponse1(buffer, bytesRead);
  parsedResponse = RESPParser::parseSimpleString(okResponse1);
  if (parsedResponse != "OK") {
    std::cerr << "Unexpected response to REPLCONF listening-port: " << parsedResponse << "\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::cout << "Sent REPLCONF listening-port\n";
  
  // Send REPLCONF capa psync2
  std::string replconfCapa = RESPParser::encodeArray({"REPLCONF", "capa", "psync2"});
  if (send(masterFd_, replconfCapa.c_str(), replconfCapa.length(), 0) < 0) {
    std::cerr << "Failed to send REPLCONF capa to master\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  // Receive OK response
  bytesRead = recv(masterFd_, buffer, sizeof(buffer), 0);
  if (bytesRead <= 0) {
    std::cerr << "Failed to receive response to REPLCONF capa\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::string okResponse2(buffer, bytesRead);
  parsedResponse = RESPParser::parseSimpleString(okResponse2);
  if (parsedResponse != "OK") {
    std::cerr << "Unexpected response to REPLCONF capa: " << parsedResponse << "\n";
    close(masterFd_);
    masterFd_ = -1;
    return false;
  }
  
  std::cout << "Sent REPLCONF capa psync2\n";
  std::cout << "Handshake with master completed successfully\n";
  
  return true;
}

void RedisServer::run() {
  if (!createServerSocket()) {
    return;
  }

  loadRDBFile();

  // If we're a replica, connect to master
  if (config_->isReplica()) {
    if (!connectToMaster()) {
      std::cerr << "Failed to connect to master, exiting\n";
      return;
    }
  }

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
  int clientFd =
      accept(serverFd_, (struct sockaddr *)&client_addr, &client_addr_len);

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

}  // namespace redis