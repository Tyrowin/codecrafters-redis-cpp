#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <set>
#include <algorithm>
#include <vector>
#include <sstream>

// Parse RESP array and extract command and arguments
std::vector<std::string> parseRESPArray(const std::string& data) {
  std::vector<std::string> result;
  std::istringstream iss(data);
  std::string line;
  
  if (!std::getline(iss, line) || line.empty() || line[0] != '*') {
    return result; // Not a valid RESP array
  }
  
  // Remove \r if present
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  
  int numElements = std::stoi(line.substr(1));
  
  for (int i = 0; i < numElements; i++) {
    // Read bulk string length line
    if (!std::getline(iss, line)) break;
    if (line.empty() || line[0] != '$') break;
    
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    
    int length = std::stoi(line.substr(1));
    
    // Read the actual string
    if (!std::getline(iss, line)) break;
    
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    
    result.push_back(line);
  }
  
  return result;
}

// Handle Redis commands
std::string handleCommand(const std::vector<std::string>& command) {
  if (command.empty()) {
    return "-ERR empty command\r\n";
  }
  
  std::string cmd = command[0];
  // Convert to uppercase for case-insensitive comparison
  std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
  
  if (cmd == "PING") {
    return "+PONG\r\n";
  } else if (cmd == "ECHO") {
    if (command.size() < 2) {
      return "-ERR wrong number of arguments for 'echo' command\r\n";
    }
    // Return as RESP bulk string
    std::string arg = command[1];
    return "$" + std::to_string(arg.length()) + "\r\n" + arg + "\r\n";
  } else {
    return "-ERR unknown command '" + command[0] + "'\r\n";
  }
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  std::cout << "Server listening on port 6379...\n";
  std::cout << "Logs from your program will appear here!\n";

  // Set to keep track of all client file descriptors
  std::set<int> client_fds;
  
  // Buffer for reading client data
  char buffer[1024];
  
  // Main event loop
  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    
    // Add server socket to the set
    FD_SET(server_fd, &read_fds);
    int max_fd = server_fd;
    
    // Add all client sockets to the set
    for (int client_fd : client_fds) {
      FD_SET(client_fd, &read_fds);
      max_fd = std::max(max_fd, client_fd);
    }
    
    // Wait for activity on any socket
    int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
    
    if (activity < 0) {
      std::cerr << "select() failed\n";
      break;
    }
    
    // Check if there's a new connection on the server socket
    if (FD_ISSET(server_fd, &read_fds)) {
      struct sockaddr_in client_addr;
      int client_addr_len = sizeof(client_addr);
      
      int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
      if (client_fd < 0) {
        std::cerr << "Failed to accept client connection\n";
        continue;
      }
      
      client_fds.insert(client_fd);
      std::cout << "New client connected (fd: " << client_fd << ")\n";
    }
    
    // Check all client sockets for incoming data
    for (auto it = client_fds.begin(); it != client_fds.end();) {
      int client_fd = *it;
      
      if (FD_ISSET(client_fd, &read_fds)) {
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received <= 0) {
          // Client disconnected or error occurred
          std::cout << "Client disconnected (fd: " << client_fd << ")\n";
          close(client_fd);
          it = client_fds.erase(it);
        } else {
          // Null-terminate the received data
          buffer[bytes_received] = '\0';
          std::string receivedData(buffer, bytes_received);
          
          // Parse the Redis command
          std::vector<std::string> command = parseRESPArray(receivedData);
          
          // Handle the command and get response
          std::string response = handleCommand(command);
          
          // Send response
          if (send(client_fd, response.c_str(), response.length(), 0) < 0) {
            std::cerr << "Failed to send response to client (fd: " << client_fd << ")\n";
            close(client_fd);
            it = client_fds.erase(it);
          } else {
            ++it;
          }
        }
      } else {
        ++it;
      }
    }
  }
  
  // Clean up all client connections
  for (int client_fd : client_fds) {
    close(client_fd);
  }
  close(server_fd);

  return 0;
}
