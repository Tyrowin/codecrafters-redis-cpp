#include "redis/RESPParser.h"

#include <sstream>

namespace redis {

std::vector<std::string> RESPParser::parseArray(const std::string& data) {
  std::vector<std::string> result;
  std::istringstream iss(data);
  std::string line;

  if (!std::getline(iss, line) || line.empty() || line[0] != '*') {
    return result;
  }

  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }

  int numElements = std::stoi(line.substr(1));

  for (int i = 0; i < numElements; i++) {
    if (!std::getline(iss, line)) break;
    if (line.empty() || line[0] != '$') break;

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    int length = std::stoi(line.substr(1));

    if (!std::getline(iss, line)) break;

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.length() != static_cast<size_t>(length)) {
      line = line.substr(0, length);
    }

    result.push_back(line);
  }

  return result;
}

std::string RESPParser::parseSimpleString(const std::string& data) {
  if (data.empty() || data[0] != '+') {
    return "";
  }

  size_t end = data.find("\r\n");
  if (end == std::string::npos) {
    return "";
  }

  return data.substr(1, end - 1);
}

std::string RESPParser::encodeSimpleString(const std::string& str) {
  return "+" + str + "\r\n";
}

std::string RESPParser::encodeBulkString(const std::string& str) {
  return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RESPParser::encodeArray(const std::vector<std::string>& items) {
  std::string result = "*" + std::to_string(items.size()) + "\r\n";
  for (const auto& item : items) {
    result += encodeBulkString(item);
  }
  return result;
}

std::string RESPParser::encodeError(const std::string& error) {
  return "-" + error + "\r\n";
}

std::string RESPParser::encodeNull() { return "$-1\r\n"; }

}  // namespace redis