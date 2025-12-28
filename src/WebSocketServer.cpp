#include "WebSocketServer.h"
#include <fstream>
#include <sstream>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

WebSocketServer::WebSocketServer(int port, const std::string& www_path)
  : port_(port), www_path_(www_path), ws_loop_(nullptr), running_(false), listen_socket_(nullptr) {
}

WebSocketServer::~WebSocketServer() {
  stop();
}

bool WebSocketServer::start() {
  if (running_) {
    return true;
  }
  
  running_ = true;
  ws_thread_ = std::thread(&WebSocketServer::run_server, this);
  
  // Give the server a moment to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  return true;
}

void WebSocketServer::stop() {
  if (!running_) {
    return;
  }
  
  running_ = false;
  
  // Just detach - don't try to interact with the loop during shutdown
  // The OS will clean up when the process exits
  if (ws_thread_.joinable()) {
    ws_thread_.detach();
  }
}

void WebSocketServer::log(const std::string& level, const std::string& message) {
  log_queue_.push_back(LogMessage(level, message));
}

void WebSocketServer::update_status(double fps, int frame_count, int elapsed_time, const std::string& state) {
  status_queue_.push_back(StatusMessage(fps, frame_count, elapsed_time, state));
}

void WebSocketServer::send_event(const std::string& event_type, const std::string& data) {
  // Format as JSON and broadcast on "events" channel
  std::ostringstream oss;
  oss << "{\"type\":\"event\",\"event\":\"" << event_type 
      << "\",\"data\":\"" << data << "\"}";
  broadcast_to_subscribers("events", oss.str());
}

bool WebSocketServer::has_command() {
  return !command_queue_.empty();
}

CommandRequest WebSocketServer::get_command() {
  CommandRequest req = command_queue_.front();
  command_queue_.pop_front();
  return req;
}

void WebSocketServer::send_response(const std::string& requestId, bool success, 
                                    const std::string& result) {
  std::ostringstream oss;
  oss << "{\"type\":\"response\",\"requestId\":\"" << requestId 
      << "\",\"status\":\"" << (success ? "ok" : "error") << "\"";
  
  if (success) {
    // Escape the result string for JSON
    std::string escaped_result;
    for (char c : result) {
      if (c == '"') escaped_result += "\\\"";
      else if (c == '\\') escaped_result += "\\\\";
      else if (c == '\n') escaped_result += "\\n";
      else if (c == '\r') escaped_result += "\\r";
      else if (c == '\t') escaped_result += "\\t";
      else escaped_result += c;
    }
    oss << ",\"result\":\"" << escaped_result << "\"";
  } else {
    // Escape the error string for JSON (same as result)
    std::string escaped_error;
    for (char c : result) {
      if (c == '"') escaped_error += "\\\"";
      else if (c == '\\') escaped_error += "\\\\";
      else if (c == '\n') escaped_error += "\\n";
      else if (c == '\r') escaped_error += "\\r";
      else if (c == '\t') escaped_error += "\\t";
      else escaped_error += c;
    }
    oss << ",\"error\":\"" << escaped_error << "\"";
  }
  
  oss << "}";
  
  std::string response_str = oss.str();
  
  // Find which client sent this request and send directly to them
  void* target_ws = nullptr;
  {
    std::lock_guard<std::mutex> lock(response_mutex_);
    auto it = response_clients_.find(requestId);
    if (it != response_clients_.end()) {
      target_ws = it->second;
      response_clients_.erase(it);  // Clean up after sending
    }
  }
  
  if (target_ws && ws_loop_) {
    ws_loop_->defer([target_ws, response_str]() {
      auto* ws = (uWS::WebSocket<false, true, WSClientData>*)target_ws;
      ws->send(response_str, uWS::OpCode::TEXT);
    });
  }
}

void WebSocketServer::flush_messages() {
  process_queued_messages();
}

void WebSocketServer::run_server() {
  ws_loop_ = uWS::Loop::get();
  
  auto app = uWS::App();
  
  // Serve static files
  app.get("/*", [this](auto* res, auto* req) {
    std::string url_path(req->getUrl());
    
    // Default to index.html
    if (url_path == "/" || url_path == "/console") {
      url_path = "/console.html";
    }
    
    std::string file_path = www_path_ + url_path;
    std::string content = read_file(file_path);
    
    if (content.empty()) {
      res->writeStatus("404 Not Found")
         ->writeHeader("Content-Type", "text/plain")
         ->end("File not found");
      return;
    }
    
    res->writeHeader("Content-Type", get_content_type(file_path))
       ->writeHeader("Cache-Control", "no-cache")
       ->end(content);
  });
  
  // WebSocket endpoint
  app.template ws<WSClientData>("/ws", {
    .compression = uWS::SHARED_COMPRESSOR,
    .maxPayloadLength = 16 * 1024 * 1024,
    .idleTimeout = 120,
    .maxBackpressure = 16 * 1024 * 1024,
    
    .upgrade = [](auto* res, auto* req, auto* context) {
      res->template upgrade<WSClientData>({},
        req->getHeader("sec-websocket-key"),
        req->getHeader("sec-websocket-protocol"),
        req->getHeader("sec-websocket-extensions"),
        context);
    },
    
    .open = [this](auto* ws) {
      WSClientData* userData = (WSClientData*)ws->getUserData();
      
      // Create response queue for this client
      userData->response_queue = new SharedQueue<std::string>();
      
      // Generate client ID
      char client_id[32];
      snprintf(client_id, sizeof(client_id), "ws_%p", (void*)ws);
      userData->client_id = client_id;
      
      // Default subscriptions
      userData->subscriptions.insert("log");
      userData->subscriptions.insert("status");
      userData->subscriptions.insert("response");  // Add response channel
      
      // Register client
      {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_[ws] = userData;
      }
      
      std::cout << "WebSocket client connected: " << userData->client_id << std::endl;
      
      // Send welcome message
      std::ostringstream oss;
      oss << "{\"type\":\"welcome\",\"client_id\":\"" << userData->client_id 
          << "\",\"message\":\"Connected to stim2 WebSocket server\"}";
      ws->send(oss.str(), uWS::OpCode::TEXT);
    },
    
    .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
      WSClientData* userData = (WSClientData*)ws->getUserData();
      
      if (!userData) {
        std::cerr << "ERROR: Invalid userData in message handler" << std::endl;
        ws->close();
        return;
      }
      
      // Parse JSON using nlohmann/json
      try {
        json msg = json::parse(message);
        
        if (!msg.contains("cmd")) {
          ws->send("{\"error\":\"Missing 'cmd' field\"}", uWS::OpCode::TEXT);
          return;
        }
        
        std::string cmd = msg["cmd"];
        
        if (cmd == "eval") {
          if (!msg.contains("script")) {
            ws->send("{\"error\":\"Missing 'script' field\"}", uWS::OpCode::TEXT);
            return;
          }
          
          std::string script = msg["script"];
          std::string requestId = msg.value("requestId", "");
          
          // Queue command for main thread
          CommandRequest req;
          req.cmd = "eval";
          req.script = script;
          req.requestId = requestId;
          req.response_queue = nullptr;
          
          // Store which client sent this request
          {
            std::lock_guard<std::mutex> lock(response_mutex_);
            response_clients_[requestId] = ws;
          }
          
          command_queue_.push_back(req);
        }
        else if (cmd == "subscribe") {
          if (msg.contains("channel")) {
            std::string channel = msg["channel"];
            userData->subscriptions.insert(channel);
            
            std::ostringstream oss;
            oss << "{\"type\":\"subscribed\",\"channel\":\"" << channel << "\"}";
            ws->send(oss.str(), uWS::OpCode::TEXT);
          }
        }
        else if (cmd == "unsubscribe") {
          if (msg.contains("channel")) {
            std::string channel = msg["channel"];
            userData->subscriptions.erase(channel);
            
            std::ostringstream oss;
            oss << "{\"type\":\"unsubscribed\",\"channel\":\"" << channel << "\"}";
            ws->send(oss.str(), uWS::OpCode::TEXT);
          }
        }
        else {
          ws->send("{\"error\":\"Unknown command\"}", uWS::OpCode::TEXT);
        }
      } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        std::ostringstream oss;
        oss << "{\"error\":\"Invalid JSON: " << e.what() << "\"}";
        ws->send(oss.str(), uWS::OpCode::TEXT);
      }
    },
    
    .close = [this](auto* ws, int code, std::string_view message) {
      WSClientData* userData = (WSClientData*)ws->getUserData();
      
      if (!userData) {
        return;
      }
      
      std::cout << "WebSocket client disconnected: " << userData->client_id << std::endl;
      
      // Remove from client list
      {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(ws);
      }
      
      // Clean up response queue
      if (userData->response_queue) {
        delete userData->response_queue;
        userData->response_queue = nullptr;
      }
    }
  }).listen("0.0.0.0", port_, [this](auto* listen_socket) {
    if (listen_socket) {
      listen_socket_ = listen_socket;  // Store for shutdown
      std::cout << "WebSocket server listening on port " << port_ << std::endl;
      std::cout << "Console available at http://localhost:" << port_ << "/console.html" << std::endl;
    } else {
      std::cerr << "FATAL: Failed to start WebSocket server on port " << port_ << std::endl;
      std::cerr << "Port may already be in use. Exiting." << std::endl;
      exit(1);
    }
  });
  
  app.run();
}

void WebSocketServer::broadcast_to_subscribers(const std::string& channel, 
                                               const std::string& message) {
  if (!ws_loop_) return;
  
  ws_loop_->defer([this, channel, message]() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto& pair : clients_) {
      auto* ws = pair.first;
      WSClientData* userData = pair.second;
      
      if (userData->subscriptions.count(channel)) {
        ((uWS::WebSocket<false, true, WSClientData>*)ws)->send(message, uWS::OpCode::TEXT);
      }
    }
  });
}

void WebSocketServer::process_queued_messages() {
  // Process log messages
  while (!log_queue_.empty()) {
    LogMessage msg = log_queue_.front();
    log_queue_.pop_front();
    
    std::string json = log_message_to_json(msg);
    broadcast_to_subscribers("log", json);
  }
  
  // Process status messages
  while (!status_queue_.empty()) {
    StatusMessage msg = status_queue_.front();
    status_queue_.pop_front();
    
    std::string json = status_message_to_json(msg);
    broadcast_to_subscribers("status", json);
  }
  
  // Responses are now sent directly to clients via send_response()
}

std::string WebSocketServer::read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return "";
  }
  
  std::ostringstream oss;
  oss << file.rdbuf();
  return oss.str();
}

std::string WebSocketServer::get_content_type(const std::string& path) {
  size_t dot_pos = path.rfind('.');
  if (dot_pos == std::string::npos) {
    return "text/plain";
  }
  
  std::string ext = path.substr(dot_pos + 1);
  
  if (ext == "html") return "text/html; charset=utf-8";
  if (ext == "css") return "text/css";
  if (ext == "js") return "application/javascript";
  if (ext == "json") return "application/json";
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "svg") return "image/svg+xml";
  
  return "text/plain";
}

std::string WebSocketServer::log_message_to_json(const LogMessage& msg) {
  std::ostringstream oss;
  
  // Escape message for JSON
  std::string escaped_msg;
  for (char c : msg.message) {
    if (c == '"') escaped_msg += "\\\"";
    else if (c == '\\') escaped_msg += "\\\\";
    else if (c == '\n') escaped_msg += "\\n";
    else if (c == '\r') escaped_msg += "\\r";
    else if (c == '\t') escaped_msg += "\\t";
    else escaped_msg += c;
  }
  
  oss << "{\"type\":\"log\",\"level\":\"" << msg.level 
      << "\",\"message\":\"" << escaped_msg 
      << "\",\"timestamp\":\"" << msg.timestamp << "\"}";
  
  return oss.str();
}

std::string WebSocketServer::status_message_to_json(const StatusMessage& msg) {
  std::ostringstream oss;
  oss << "{\"type\":\"status\",\"fps\":" << msg.fps 
      << ",\"frames\":" << msg.frame_count 
      << ",\"time\":" << msg.elapsed_time
      << ",\"state\":\"" << msg.state << "\"}";
  return oss.str();
}