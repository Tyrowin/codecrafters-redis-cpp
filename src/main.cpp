#include <iostream>
#include <memory>

#include "redis/Config.h"
#include "redis/RedisServer.h"

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  auto config = std::make_shared<redis::Config>();
  config->parseArgs(argc, argv);

  redis::RedisServer server(config);
  server.run();

  return 0;
}