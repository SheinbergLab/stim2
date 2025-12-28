#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <string>
#include <thread>
#include <mutex>
#include <set>
#include <map>
#include <deque>
#include <condition_variable>
#include <atomic>
#include "App.h"
#include "SharedQueue.h"

// Message structures
struct LogMessage {
  std::string level;      // "info", "warn", "error", "debug"
  std::string message;
  std::string timestamp;
  
  LogMessage() {}
  LogMessage(const std::string& lvl, const std::string& msg)
    : level(lvl), message(msg) {
    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
    
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&time_t));
    char timestamp_buf[64];
    snprintf(timestamp_buf, sizeof(timestamp_buf), "%s.%03d", buf, (int)ms.count());
    timestamp = timestamp_buf;
  }
};

struct StatusMessage {
  double fps;
  int frame_count;      // Total frames rendered
  int elapsed_time;     // Elapsed time in seconds
  std::string state;    // "idle", "running", "paused"
  
  StatusMessage() : fps(0), frame_count(0), elapsed_time(0), state("idle") {}
  StatusMessage(double f, int fc, int et, const std::string& s)
    : fps(f), frame_count(fc), elapsed_time(et), state(s) {}
};

struct CommandRequest {
  std::string cmd;
  std::string script;
  std::string requestId;
  SharedQueue<std::string>* response_queue;
  
  CommandRequest() : response_queue(nullptr) {}
};

// Per-client WebSocket data
struct WSClientData {
  std::string client_id;
  std::set<std::string> subscriptions;  // Channels: "log", "status", "events"
  SharedQueue<std::string>* response_queue;
  
  WSClientData() : response_queue(nullptr) {}
  ~WSClientData() {
    // Don't delete here - we'll manage it in .close handler
  }
};

class WebSocketServer {
public:
  WebSocketServer(int port, const std::string& www_path);
  ~WebSocketServer();
  
  // Start/stop the server
  bool start();
  void stop();
  
  // Push messages to queues (called from main thread)
  void log(const std::string& level, const std::string& message);
  void update_status(double fps, int frame_count, int elapsed_time, const std::string& state);
  void send_event(const std::string& event_type, const std::string& data);
  
  // Check for incoming commands (called from main thread)
  bool has_command();
  CommandRequest get_command();
  
  // Send command response (called from main thread)
  void send_response(const std::string& requestId, bool success, 
                     const std::string& result);
  
  // Process queued messages (call periodically from main thread)
  void flush_messages();
  
private:
  int port_;
  std::string www_path_;
  uWS::Loop* ws_loop_;
  std::thread ws_thread_;
  std::atomic<bool> running_;
  void* listen_socket_;  // Store listen socket for shutdown
  
  // Message queues
  SharedQueue<LogMessage> log_queue_;
  SharedQueue<StatusMessage> status_queue_;
  SharedQueue<CommandRequest> command_queue_;
  
  // Response routing
  std::mutex response_mutex_;
  std::map<std::string, void*> response_clients_;  // requestId -> ws pointer
  
  // Client tracking
  std::mutex clients_mutex_;
  std::map<void*, WSClientData*> clients_;
  
  // Server thread
  void run_server();
  
  // Message broadcasting
  void broadcast_to_subscribers(const std::string& channel, const std::string& message);
  void process_queued_messages();
  
  // Utility functions
  std::string read_file(const std::string& path);
  std::string get_content_type(const std::string& path);
  std::string log_message_to_json(const LogMessage& msg);
  std::string status_message_to_json(const StatusMessage& msg);
};

// Forward declaration for Application access
class Application;
extern Application app;

// Logging helper (replaces fprintf calls)
// Uses app.ws_server internally
void log_message(const char* level, const char* fmt, ...);

#endif // WEBSOCKET_SERVER_H