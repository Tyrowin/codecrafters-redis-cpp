#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H

#include <memory>
#include <set>

namespace redis {

class Config;
class Storage;
class CommandHandler;
class RDBParser;

class RedisServer {
 public:
  RedisServer(std::shared_ptr<Config> config);
  ~RedisServer();

  void run();

 private:
  bool loadRDBFile();
  std::shared_ptr<Config> config_;
  std::shared_ptr<Storage> storage_;
  std::shared_ptr<CommandHandler> commandHandler_;

  int serverFd_;
  std::set<int> clientFds_;
  int masterFd_;

  bool createServerSocket();
  bool connectToMaster();
  void handleNewConnection();
  void handleClientData(int clientFd);
  void closeClient(int clientFd);
};

}  // namespace redis

#endif  // REDIS_SERVER_H