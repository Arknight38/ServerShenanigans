#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>

// For compression (zlib)
#include <zlib.h>

// For SHA256
#include <openssl/sha.h>
#include <openssl/evp.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "libcrypto.lib")

namespace fs = std::filesystem;

const int DEFAULT_PORT = 8080;
const int CHUNK_SIZE = 65536; // Larger buffer: 64KB
const std::string CONFIG_FILE = "server_config.txt";
const int MAX_CONNECTIONS = 50;

struct FileInfo {
    std::string filename;
    std::string filepath;
    size_t filesize;
    std::string sha256;
};

struct ServerConfig {
    int port = DEFAULT_PORT;
    bool enableCompression = true;
    int maxConnections = MAX_CONNECTIONS;
    std::string sharedFolder = "";
    
    void load() {
        std::ifstream file(CONFIG_FILE);
        if (!file) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                
                if (key == "port") port = std::stoi(value);
                else if (key == "compression") enableCompression = (value == "true");
                else if (key == "max_connections") maxConnections = std::stoi(value);
                else if (key == "shared_folder") sharedFolder = value;
            }
        }
    }
    
    void save() {
        std::ofstream file(CONFIG_FILE);
        file << "# Server Configuration\n";
        file << "port=" << port << "\n";
        file << "compression=" << (enableCompression ? "true" : "false") << "\n";
        file << "max_connections=" << maxConnections << "\n";
        file << "shared_folder=" << sharedFolder << "\n";
    }
};

// Tab completion helper class
class PathCompleter {
private:
    std::vector<std::string> matches;
    size_t currentMatch;
    
public:
    PathCompleter() : currentMatch(0) {}
    
    std::vector<std::string> findMatches(const std::string& partial, bool foldersOnly = false) {
        matches.clear();
        currentMatch = 0;
        
        std::string searchPath = partial;
        std::string prefix = "";
        
        // Extract directory and filename parts
        size_t lastSlash = partial.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            searchPath = partial.substr(0, lastSlash + 1);
            prefix = partial.substr(lastSlash + 1);
        } else {
            searchPath = ".";
        }
        
        try {
            if (!fs::exists(searchPath)) {
                return matches;
            }
            
            for (const auto& entry : fs::directory_iterator(searchPath)) {
                std::string name = entry.path().filename().string();
                
                // Skip hidden files
                if (name[0] == '.') continue;
                
                // Check if folders only mode
                if (foldersOnly && !entry.is_directory()) continue;
                
                // Check if matches prefix
                if (prefix.empty() || 
                    name.substr(0, prefix.length()) == prefix ||
                    (name.length() >= prefix.length() && 
                     std::equal(prefix.begin(), prefix.end(), name.begin(),
                               [](char a, char b) { return tolower(a) == tolower(b); }))) {
                    
                    std::string fullPath;
                    if (lastSlash != std::string::npos) {
                        fullPath = partial.substr(0, lastSlash + 1) + name;
                    } else {
                        fullPath = name;
                    }
                    
                    if (entry.is_directory()) {
                        fullPath += "\\";
                    }
                    
                    matches.push_back(fullPath);
                }
            }
            
            std::sort(matches.begin(), matches.end());
            
        } catch (const std::exception&) {
            // Ignore errors
        }
        
        return matches;
    }
    
    std::string getNextMatch() {
        if (matches.empty()) return "";
        
        std::string result = matches[currentMatch];
        currentMatch = (currentMatch + 1) % matches.size();
        return result;
    }
    
    bool hasMatches() const {
        return !matches.empty();
    }
    
    size_t getMatchCount() const {
        return matches.size();
    }
};

// Enhanced input reader with tab completion
std::string readLineWithCompletion(const std::string& prompt) {
    std::cout << prompt << std::flush;
    
    std::string input;
    PathCompleter completer;
    std::string lastPartial;
    bool inCompletion = false;
    
    while (true) {
        int ch = _getch();
        
        // Enter
        if (ch == '\r' || ch == '\n') {
            std::cout << std::endl;
            return input;
        }
        // Backspace
        else if (ch == '\b' || ch == 127) {
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b" << std::flush;
                inCompletion = false;
            }
        }
        // Tab
        else if (ch == '\t') {
            // Determine if we're in add/addfolder/setfolder command
            std::string cmd;
            std::string path;
            bool foldersOnly = false;
            
            if (input.find("add ") == 0) {
                cmd = "add ";
                path = input.substr(4);
            } else if (input.find("addfolder ") == 0) {
                cmd = "addfolder ";
                path = input.substr(10);
                foldersOnly = true;
            } else if (input.find("setfolder ") == 0) {
                cmd = "setfolder ";
                path = input.substr(10);
                foldersOnly = true;
            } else {
                continue; // No completion for other commands
            }
            
            // Find matches if not in completion mode or path changed
            if (!inCompletion || path != lastPartial) {
                completer.findMatches(path, foldersOnly);
                lastPartial = path;
                inCompletion = true;
            }
            
            if (completer.hasMatches()) {
                // Clear current line
                for (size_t i = 0; i < prompt.length() + input.length(); i++) {
                    std::cout << "\b \b";
                }
                
                // Get next match
                std::string match = completer.getNextMatch();
                input = cmd + match;
                
                // Show match count if multiple
                if (completer.getMatchCount() > 1) {
                    std::cout << prompt << input 
                             << "  [" << completer.getMatchCount() << " matches]" << std::flush;
                    
                    // Clear the match count display
                    std::string matchInfo = "  [" + std::to_string(completer.getMatchCount()) + " matches]";
                    for (size_t i = 0; i < matchInfo.length(); i++) {
                        std::cout << "\b \b";
                    }
                } else {
                    std::cout << prompt << input << std::flush;
                }
            }
        }
        // Escape (cancel completion)
        else if (ch == 27) {
            inCompletion = false;
        }
        // Regular character
        else if (ch >= 32 && ch < 127) {
            input += (char)ch;
            std::cout << (char)ch << std::flush;
            inCompletion = false;
        }
    }
}

class P2PFileServer {
private:
    SOCKET serverSocket;
    std::map<std::string, FileInfo> sharedFiles;
    std::mutex filesMutex;
    std::atomic<bool> running;
    std::atomic<int> activeConnections;
    ServerConfig config;
    bool wsaInitialized;
    
    std::string calculateSHA256(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) return "";
        
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (!context) return "";
        
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(context);
            return "";
        }
        
        char buffer[CHUNK_SIZE];
        while (file.read(buffer, CHUNK_SIZE) || file.gcount() > 0) {
            if (EVP_DigestUpdate(context, buffer, file.gcount()) != 1) {
                EVP_MD_CTX_free(context);
                return "";
            }
        }
        
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;
        
        if (EVP_DigestFinal_ex(context, hash, &hashLen) != 1) {
            EVP_MD_CTX_free(context);
            return "";
        }
        
        EVP_MD_CTX_free(context);
        
        std::stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        
        return ss.str();
    }
    
    std::string getLocalIP() {
        char hostName[256];
        if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR) {
            return "Unknown";
        }
        
        struct addrinfo hints, *result;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostName, NULL, &hints, &result) != 0) {
            return "Unknown";
        }
        
        char ipStr[INET_ADDRSTRLEN];
        struct sockaddr_in* sockaddr = (struct sockaddr_in*)result->ai_addr;
        inet_ntop(AF_INET, &(sockaddr->sin_addr), ipStr, INET_ADDRSTRLEN);
        
        freeaddrinfo(result);
        return std::string(ipStr);
    }
    
    std::vector<char> compressData(const char* data, size_t size, size_t& compressedSize) {
        compressedSize = compressBound(size);
        std::vector<char> compressed(compressedSize);
        
        if (compress2((Bytef*)compressed.data(), (uLongf*)&compressedSize, 
                     (const Bytef*)data, size, Z_BEST_SPEED) != Z_OK) {
            compressedSize = 0;
            return std::vector<char>();
        }
        
        compressed.resize(compressedSize);
        return compressed;
    }
    
public:
    P2PFileServer() : serverSocket(INVALID_SOCKET), running(false), 
                      activeConnections(0), wsaInitialized(false) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
        } else {
            wsaInitialized = true;
        }
        
        config.load();
    }
    
    ~P2PFileServer() {
        stop();
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
        }
        if (wsaInitialized) {
            WSACleanup();
        }
    }
    
    bool startServer() {
        if (!wsaInitialized) {
            std::cerr << "Winsock not initialized\n";
            return false;
        }
        
        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Socket creation failed: " << WSAGetLastError() << "\n";
            return false;
        }
        
        char opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(config.port);
        
        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
            closesocket(serverSocket);
            return false;
        }
        
        if (listen(serverSocket, config.maxConnections) == SOCKET_ERROR) {
            std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
            closesocket(serverSocket);
            return false;
        }
        
        running = true;
        
        std::cout << "\n========================================\n";
        std::cout << "FILE SHARING SERVER STARTED\n";
        std::cout << "========================================\n";
        std::cout << "Local IP: " << getLocalIP() << "\n";
        std::cout << "Port: " << config.port << "\n";
        std::cout << "Compression: " << (config.enableCompression ? "Enabled" : "Disabled") << "\n";
        std::cout << "Max Connections: " << config.maxConnections << "\n";
        std::cout << "========================================\n\n";
        
        if (!config.sharedFolder.empty() && fs::exists(config.sharedFolder)) {
            std::cout << "Auto-loading shared folder...\n";
            addFolder(config.sharedFolder);
        }
        
        return true;
    }
    
    void addSharedFile(const std::string& filepath) {
        if (!fs::exists(filepath)) {
            std::cerr << "File does not exist: " << filepath << std::endl;
            return;
        }
        
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Cannot open file: " << filepath << std::endl;
            return;
        }
        
        size_t filesize = file.tellg();
        file.close();
        
        FileInfo info;
        info.filename = fs::path(filepath).filename().string();
        info.filepath = filepath;
        info.filesize = filesize;
        
        std::cout << "[HASHING] " << info.filename << "... " << std::flush;
        info.sha256 = calculateSHA256(filepath);
        std::cout << "Done\n";
        
        std::lock_guard<std::mutex> lock(filesMutex);
        sharedFiles[info.filename] = info;
        
        std::cout << "[SHARED] " << info.filename 
                  << " (" << filesize << " bytes)\n";
    }
    
    void addFolder(const std::string& folderPath) {
        try {
            if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
                std::cerr << "Invalid folder: " << folderPath << std::endl;
                return;
            }
            
            int count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
                if (entry.is_regular_file()) {
                    addSharedFile(entry.path().string());
                    count++;
                }
            }
            
            std::cout << "[INFO] Added " << count << " files from folder\n";
        } catch (const std::exception& e) {
            std::cerr << "Error reading folder: " << e.what() << std::endl;
        }
    }
    
    void removeFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(filesMutex);
        auto it = sharedFiles.find(filename);
        if (it != sharedFiles.end()) {
            sharedFiles.erase(it);
            std::cout << "[REMOVED] " << filename << "\n";
        } else {
            std::cout << "[ERROR] File not found: " << filename << "\n";
        }
    }
    
    void handleClient(SOCKET clientSocket, std::string clientIP) {
        activeConnections++;
        
        char buffer[4096] = {0};
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead <= 0) {
            closesocket(clientSocket);
            activeConnections--;
            return;
        }
        
        std::string request(buffer);
        std::cout << "[REQUEST] " << clientIP << " - " << request << std::flush;
        
        if (request.find("LIST") == 0) {
            handleListRequest(clientSocket);
        }
        else if (request.find("GET ") == 0) {
            std::string params = request.substr(4);
            params.erase(params.find_last_not_of(" \n\r\t") + 1);
            
            // Parse: GET filename [OFFSET offset] [COMPRESS]
            std::string filename;
            size_t offset = 0;
            bool compress = false;
            
            std::istringstream iss(params);
            iss >> filename;
            
            std::string token;
            while (iss >> token) {
                if (token == "OFFSET") {
                    iss >> offset;
                } else if (token == "COMPRESS") {
                    compress = true;
                }
            }
            
            handleGetRequest(clientSocket, filename, offset, compress, clientIP);
        }
        else if (request.find("CHECKSUM ") == 0) {
            std::string filename = request.substr(9);
            filename.erase(filename.find_last_not_of(" \n\r\t") + 1);
            handleChecksumRequest(clientSocket, filename);
        }
        
        closesocket(clientSocket);
        activeConnections--;
    }
    
    void handleListRequest(SOCKET clientSocket) {
        std::string response;
        std::lock_guard<std::mutex> lock(filesMutex);
        
        if (sharedFiles.empty()) {
            response = "No files available\n";
        } else {
            response = "Available files:\n";
            for (const auto& pair : sharedFiles) {
                response += pair.second.filename + ":" + 
                           std::to_string(pair.second.filesize) + ":" +
                           pair.second.sha256 + "\n";
            }
        }
        
        send(clientSocket, response.c_str(), (int)response.length(), 0);
    }
    
    void handleChecksumRequest(SOCKET clientSocket, const std::string& filename) {
        std::lock_guard<std::mutex> lock(filesMutex);
        auto it = sharedFiles.find(filename);
        
        if (it == sharedFiles.end()) {
            std::string response = "ERROR: File not found\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        } else {
            std::string response = "CHECKSUM:" + it->second.sha256 + "\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
        }
    }
    
    void handleGetRequest(SOCKET clientSocket, const std::string& filename, 
                         size_t offset, bool compress, const std::string& clientIP) {
        std::lock_guard<std::mutex> lock(filesMutex);
        auto it = sharedFiles.find(filename);
        
        if (it == sharedFiles.end()) {
            std::string response = "ERROR: File not found\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
            return;
        }
        
        sendFile(clientSocket, it->second, offset, compress, clientIP);
    }
    
    void sendFile(SOCKET clientSocket, const FileInfo& fileInfo, 
                 size_t offset, bool compress, const std::string& clientIP) {
        std::ifstream file(fileInfo.filepath, std::ios::binary);
        if (!file) {
            std::string response = "ERROR: Cannot open file\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
            return;
        }
        
        file.seekg(0, std::ios::end);
        size_t filesize = file.tellg();
        
        if (offset >= filesize) {
            std::string response = "ERROR: Invalid offset\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
            return;
        }
        
        file.seekg(offset, std::ios::beg);
        size_t remaining = filesize - offset;
        
        compress = compress && config.enableCompression;
        
        std::stringstream ss;
        ss << "OK:" << remaining << ":" << (compress ? "COMPRESSED" : "RAW") << "\n";
        std::string response = ss.str();
        send(clientSocket, response.c_str(), (int)response.length(), 0);
        
        std::cout << "[SENDING] " << fileInfo.filename << " to " << clientIP 
                  << " (offset:" << offset << ", size:" << remaining 
                  << ", compress:" << (compress ? "yes" : "no") << ")\n";
        
        char buffer[CHUNK_SIZE];
        size_t totalSent = 0;
        
        while (file.read(buffer, CHUNK_SIZE) || file.gcount() > 0) {
            size_t bytesRead = file.gcount();
            
            if (compress) {
                size_t compressedSize;
                std::vector<char> compressed = compressData(buffer, bytesRead, compressedSize);
                
                if (compressedSize > 0) {
                    // Send compressed size first (4 bytes)
                    uint32_t size = (uint32_t)compressedSize;
                    send(clientSocket, (char*)&size, sizeof(size), 0);
                    send(clientSocket, compressed.data(), (int)compressedSize, 0);
                    totalSent += bytesRead;
                } else {
                    break;
                }
            } else {
                int sent = send(clientSocket, buffer, (int)bytesRead, 0);
                if (sent == SOCKET_ERROR) break;
                totalSent += sent;
            }
        }
        
        file.close();
        std::cout << "[COMPLETE] Sent " << totalSent << " bytes to " << clientIP << "\n";
    }
    
    void acceptConnections() {
        while (running) {
            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            
            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
            
            if (clientSocket == INVALID_SOCKET) {
                if (running) {
                    std::cerr << "[ERROR] Accept failed: " << WSAGetLastError() << "\n";
                }
                continue;
            }
            
            if (activeConnections >= config.maxConnections) {
                std::string response = "ERROR: Server busy\n";
                send(clientSocket, response.c_str(), (int)response.length(), 0);
                closesocket(clientSocket);
                continue;
            }
            
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            
            std::cout << "\n[CONNECTED] " << clientIP 
                      << " (Active: " << activeConnections + 1 << ")\n";
            
            std::thread(&P2PFileServer::handleClient, this, clientSocket, 
                       std::string(clientIP)).detach();
        }
    }
    
    void listFiles() {
        std::lock_guard<std::mutex> lock(filesMutex);
        
        if (sharedFiles.empty()) {
            std::cout << "No files shared.\n";
            return;
        }
        
        std::cout << "\nShared Files (" << sharedFiles.size() << " total):\n";
        std::cout << "----------------------------------------\n";
        for (const auto& pair : sharedFiles) {
            double sizeMB = pair.second.filesize / (1024.0 * 1024.0);
            std::cout << pair.second.filename << " - " 
                      << std::fixed << std::setprecision(2) << sizeMB << " MB\n";
            std::cout << "  SHA256: " << pair.second.sha256.substr(0, 16) << "...\n";
        }
        std::cout << "----------------------------------------\n";
    }
    
    void stop() {
        running = false;
    }
    
    void setPort(int p) { config.port = p; config.save(); }
    void setCompression(bool enable) { config.enableCompression = enable; config.save(); }
    void setSharedFolder(const std::string& folder) { 
        config.sharedFolder = folder; 
        config.save(); 
    }
};

int main(int argc, char* argv[]) {
    P2PFileServer server;
    
    if (!server.startServer()) {
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }
    
    std::thread acceptThread(&P2PFileServer::acceptConnections, &server);
    acceptThread.detach();
    
    std::cout << "\nCommands:\n";
    std::cout << "  add <filepath>         - Share a file (TAB to autocomplete)\n";
    std::cout << "  addfolder <path>       - Share a folder (TAB to autocomplete)\n";
    std::cout << "  remove <filename>      - Remove a file\n";
    std::cout << "  list                   - List shared files\n";
    std::cout << "  setfolder <path>       - Set auto-share folder (TAB to autocomplete)\n";
    std::cout << "  compress on/off        - Toggle compression\n";
    std::cout << "  quit                   - Exit\n\n";
    
    while (true) {
        std::string command = readLineWithCompletion("> ");
        
        if (command == "quit" || command == "exit") {
            server.stop();
            break;
        } 
        else if (command == "list") {
            server.listFiles();
        } 
        else if (command.find("add ") == 0) {
            server.addSharedFile(command.substr(4));
        } 
        else if (command.find("addfolder ") == 0) {
            server.addFolder(command.substr(10));
        }
        else if (command.find("remove ") == 0) {
            server.removeFile(command.substr(7));
        }
        else if (command.find("setfolder ") == 0) {
            server.setSharedFolder(command.substr(10));
            std::cout << "Folder set. Will auto-load on next start.\n";
        }
        else if (command == "compress on") {
            server.setCompression(true);
            std::cout << "Compression enabled.\n";
        }
        else if (command == "compress off") {
            server.setCompression(false);
            std::cout << "Compression disabled.\n";
        }
        else if (!command.empty()) {
            std::cout << "Unknown command.\n";
        }
    }
    
    return 0;
}