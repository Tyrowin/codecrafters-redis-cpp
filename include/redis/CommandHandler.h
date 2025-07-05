#ifndef REDIS_COMMAND_HANDLER_H
#define REDIS_COMMAND_HANDLER_H

#include <memory>
#include <string>
#include <vector>

namespace redis {

class Config;
class Storage;

class CommandHandler {
 public:
  CommandHandler(std::shared_ptr<Config> config,
                 std::shared_ptr<Storage> storage);

  std::string handleCommand(const std::vector<std::string> &command);

 private:
  std::shared_ptr<Config> config_;
  std::shared_ptr<Storage> storage_;

  std::string handlePing();
  std::string handleEcho(const std::vector<std::string> &args);
  std::string handleSet(const std::vector<std::string> &args);
  std::string handleGet(const std::vector<std::string> &args);
  std::string handleConfig(const std::vector<std::string> &args);
  std::string handleKeys(const std::vector<std::string> &args);
  std::string handleInfo(const std::vector<std::string> &args);
};

}  // namespace redis

#endif  // REDIS_COMMAND_HANDLER_H