#include "redis/Config.h"
#include <cstring>

namespace redis {

Config::Config() 
    : dir_("."), 
      dbfilename_("dump.rdb"), 
      port_(6379) {
}

void Config::parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            dir_ = argv[++i];
        } else if (std::strcmp(argv[i], "--dbfilename") == 0 && i + 1 < argc) {
            dbfilename_ = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port_ = std::stoi(argv[++i]);
        }
    }
}

} // namespace redis