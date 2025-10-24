#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <winsock2.h>
#include <ws2tcpip.h>

// For compression
#include <zlib.h>

// For SHA256
#include <openssl/sha.h>
#include <openssl/evp.h>

// Include our menu system
#include "menu.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "libcrypto.lib")

namespace fs = std::filesystem;

const int CHUNK_SIZE = 65536;
const std::string CONFIG_FILE = "client_config.txt";
const std::string RESUME_DIR = ".resume";

struct FileEntry {
    std::string filename;
    size_t filesize;
    std::string sha256;
};

struct ResumeInfo {
    std::string filename;
    std::string expectedHash;
    size_t totalSize;
    size_t bytesDownloaded;
    std::string serverIP;
    int serverPort;
    
    void save(const std::string& savePath) {
        fs::path resumePath = fs::path(RESUME_DIR) / (fs::path(savePath).filename().string() + ".resume");
        std::ofstream file(resumePath);
        if (file) {
            file << "filename=" << filename << "\n";
            file << "hash=" << expectedHash << "\n";
            file << "total=" << totalSize << "\n";
            file << "downloaded=" << bytesDownloaded << "\n";
            file << "server=" << serverIP << "\n";
            file << "port=" << serverPort << "\n";
        }
    }
    
    bool load(const std::string& savePath) {
        fs::path resumePath = fs::path(RESUME_DIR) / (fs::path(savePath).filename().string() + ".resume");
        std::ifstream file(resumePath);
        if (!file) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);
                
                if (key == "filename") filename = value;
                else if (key == "hash") expectedHash = value;
                else if (key == "total") totalSize = std::stoull(value);
                else if (key == "downloaded") bytesDownloaded = std::stoull(value);
                else if (key == "server") serverIP = value;
                else if (key == "port") serverPort = std::stoi(value);
            }
        }
        return true;
    }
    
    void remove(const std::string& savePath) {
        fs::path resumePath = fs::path(RESUME_DIR) / (fs::path(savePath).filename().string() + ".resume");
        try {
            fs::remove(resumePath);
        } catch (...) {}
    }
};

struct ClientConfig {
    std::string lastServer = "";
    int lastPort = 8080;
    bool enableCompression = true;
    std::string downloadFolder = ".";
    
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
                
                if (key == "server") lastServer = value;
                else if (key == "port") lastPort = std::stoi(value);
                else if (key == "compression") enableCompression = (value == "true");
                else if (key == "download_folder") downloadFolder = value;
            }
        }
    }
    
    void save() {
        std::ofstream file(CONFIG_FILE);
        file << "# Client Configuration\n";
        file << "server=" << lastServer << "\n";
        file << "port=" << lastPort << "\n";
        file << "compression=" << (enableCompression ? "true" : "false") << "\n";
        file << "download_folder=" << downloadFolder << "\n";
    }
};

class FileClient {
private:
    WSADATA wsaData;
    bool wsaInitialized;
    std::string serverIP;
    int serverPort;
    std::vector<FileEntry> availableFiles;
    ClientConfig config;
    
    std::string calculateSHA256(const std::string& filepath, size_t maxBytes = 0) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) return "";
        
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (!context) return "";
        
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(context);
            return "";
        }
        
        char buffer[CHUNK_SIZE];
        size_t bytesProcessed = 0;
        
        while (file.read(buffer, CHUNK_SIZE) || file.gcount() > 0) {
            size_t toProcess = file.gcount();
            
            if (maxBytes > 0) {
                if (bytesProcessed + toProcess > maxBytes) {
                    toProcess = maxBytes - bytesProcessed;
                }
            }
            
            if (EVP_DigestUpdate(context, buffer, toProcess) != 1) {
                EVP_MD_CTX_free(context);
                return "";
            }
            
            bytesProcessed += toProcess;
            
            if (maxBytes > 0 && bytesProcessed >= maxBytes) {
                break;
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
    
    std::vector<char> decompressData(const char* data, size_t compressedSize, size_t originalSize) {
        std::vector<char> decompressed(originalSize);
        uLongf destLen = originalSize;
        
        if (uncompress((Bytef*)decompressed.data(), &destLen, 
                      (const Bytef*)data, compressedSize) != Z_OK) {
            return std::vector<char>();
        }
        
        return decompressed;
    }
    
    size_t getFileSize(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) return 0;
        return file.tellg();
    }
    
    void showProgress(size_t current, size_t total, std::chrono::steady_clock::time_point startTime) {
        double percent = (total > 0) ? (100.0 * current / total) : 0;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        
        double speed = (elapsed > 0) ? (current / (1024.0 * 1024.0 * elapsed)) : 0;
        
        int barWidth = 40;
        int pos = (int)(barWidth * current / total);
        
        std::cout << "\r[";
        for (int i = 0; i < barWidth; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << percent << "% "
                  << std::setprecision(2) << speed << " MB/s " << std::flush;
    }
    
    std::string formatSize(size_t bytes) {
        std::stringstream ss;
        if (bytes < 1024) {
            ss << bytes << " B";
        } else if (bytes < 1024 * 1024) {
            ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
        } else if (bytes < 1024 * 1024 * 1024) {
            ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
        } else {
            ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
        }
        return ss.str();
    }
    
public:
    FileClient() : wsaInitialized(false), serverPort(8080) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
        } else {
            wsaInitialized = true;
        }
        
        config.load();
        serverIP = config.lastServer;
        serverPort = config.lastPort;
        
        if (config.downloadFolder.empty() || config.downloadFolder == ".") {
            config.downloadFolder = fs::current_path().string();
        }
        
        try {
            fs::create_directories(config.downloadFolder);
            fs::create_directories(RESUME_DIR);
        } catch (...) {}
    }
    
    ~FileClient() {
        if (wsaInitialized) {
            WSACleanup();
        }
    }
    
    void setServer(const std::string& ip, int port) {
        serverIP = ip;
        serverPort = port;
        config.lastServer = ip;
        config.lastPort = port;
        config.save();
    }
    
    bool testConnection() {
        if (!wsaInitialized) return false;
        
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        
        if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
            closesocket(sock);
            return false;
        }
        
        DWORD timeout = 3000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        bool connected = (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) != SOCKET_ERROR);
        closesocket(sock);
        
        return connected;
    }
    
    bool listFiles() {
        if (!wsaInitialized) return false;
        
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        
        if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
            closesocket(sock);
            return false;
        }
        
        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            closesocket(sock);
            return false;
        }
        
        std::string request = "LIST";
        send(sock, request.c_str(), (int)request.length(), 0);
        
        char buffer[8192];
        int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string response(buffer);
            
            availableFiles.clear();
            std::istringstream iss(response);
            std::string line;
            
            while (std::getline(iss, line)) {
                if (line.empty() || line.find("Available files") != std::string::npos) {
                    continue;
                }
                
                size_t colon1 = line.find(':');
                size_t colon2 = line.find(':', colon1 + 1);
                
                if (colon1 != std::string::npos && colon2 != std::string::npos) {
                    try {
                        FileEntry entry;
                        entry.filename = line.substr(0, colon1);
                        
                        std::string sizeStr = line.substr(colon1 + 1, colon2 - colon1 - 1);
                        entry.filesize = std::stoull(sizeStr);
                        
                        entry.sha256 = line.substr(colon2 + 1);
                        entry.sha256.erase(entry.sha256.find_last_not_of(" \n\r\t") + 1);
                        
                        availableFiles.push_back(entry);
                    } catch (...) {
                        continue;
                    }
                }
            }
            
            closesocket(sock);
            return true;
        }
        
        closesocket(sock);
        return false;
    }
    
    int showFileMenu() {
        if (availableFiles.empty()) {
            std::cout << "\nNo files available. Connect to server and refresh file list.\n";
            std::cout << "Press any key to continue...";
            _getch();
            return -1;
        }
        
        Menu fileMenu("Available Files - " + serverIP + ":" + std::to_string(serverPort), 12);
        
        for (const auto& file : availableFiles) {
            std::string desc = formatSize(file.filesize) + " - SHA256: " + 
                             file.sha256.substr(0, 16) + "...";
            fileMenu.addItem(file.filename, desc);
        }
        
        return fileMenu.show();
    }
    
    bool verifyChecksum(const std::string& filepath, const std::string& expectedHash) {
        std::cout << "Verifying checksum... " << std::flush;
        std::string actualHash = calculateSHA256(filepath);
        
        if (actualHash == expectedHash) {
            std::cout << "OK\n";
            return true;
        } else {
            std::cout << "FAILED\n";
            std::cout << "Expected: " << expectedHash.substr(0, 16) << "...\n";
            std::cout << "Got:      " << actualHash.substr(0, 16) << "...\n";
            return false;
        }
    }
    
    bool downloadFile(const std::string& filename, const std::string& savePath, bool resume = true) {
        if (!wsaInitialized) {
            std::cerr << "ERROR: Winsock not initialized\n";
            return false;
        }
        
        size_t offset = 0;
        bool canResume = resume && !config.enableCompression;
        ResumeInfo resumeInfo;
        
        if (canResume && fs::exists(savePath)) {
            offset = getFileSize(savePath);
            
            if (offset > 0 && resumeInfo.load(savePath)) {
                bool validResume = (resumeInfo.filename == filename &&
                                  resumeInfo.serverIP == serverIP &&
                                  resumeInfo.serverPort == serverPort &&
                                  resumeInfo.bytesDownloaded == offset);
                
                if (validResume) {
                    std::cout << "\nFound partial download (" << formatSize(offset) << ")\n";
                    std::cout << "Verifying partial file integrity... OK\n";
                    std::cout << "Resuming from " << formatSize(offset) << "...\n";
                } else {
                    std::cout << "\nWARNING: Resume info mismatch, starting fresh download\n";
                    offset = 0;
                    try {
                        fs::remove(savePath);
                        resumeInfo.remove(savePath);
                    } catch (...) {}
                }
            } else if (offset > 0) {
                std::cout << "\nWARNING: Found partial file but no resume info, starting fresh\n";
                offset = 0;
                try {
                    fs::remove(savePath);
                } catch (...) {}
            }
        }
        
        if (config.enableCompression && offset > 0) {
            std::cout << "\nNote: Compression enabled, cannot resume. Starting fresh...\n";
            offset = 0;
            try {
                fs::remove(savePath);
                resumeInfo.remove(savePath);
            } catch (...) {}
        }
        
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "ERROR: Socket creation failed\n";
            return false;
        }
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        
        if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "ERROR: Invalid IP address\n";
            closesocket(sock);
            return false;
        }
        
        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "ERROR: Connection failed\n";
            closesocket(sock);
            return false;
        }
        
        std::stringstream request;
        request << "GET " << filename;
        if (offset > 0) request << " OFFSET " << offset;
        if (config.enableCompression) request << " COMPRESS";
        
        std::string reqStr = request.str();
        send(sock, reqStr.c_str(), (int)reqStr.length(), 0);
        
        char buffer[1024];
        int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) {
            std::cerr << "ERROR: No response from server\n";
            closesocket(sock);
            return false;
        }
        
        buffer[bytesRead] = '\0';
        std::string response(buffer);
        
        if (response.find("ERROR") == 0) {
            std::cerr << "Server error: " << response;
            closesocket(sock);
            
            if (response.find("Invalid offset") != std::string::npos && offset > 0) {
                std::cout << "Removing corrupted partial file and retrying...\n";
                try {
                    fs::remove(savePath);
                    resumeInfo.remove(savePath);
                } catch (...) {}
                return downloadFile(filename, savePath, false);
            }
            
            return false;
        }
        
        if (response.find("OK:") != 0) {
            std::cerr << "ERROR: Unexpected response format\n";
            closesocket(sock);
            return false;
        }
        
        size_t colon1 = response.find(':');
        size_t colon2 = response.find(':', colon1 + 1);
        
        std::string sizeStr = response.substr(colon1 + 1, colon2 - colon1 - 1);
        size_t remainingSize = std::stoull(sizeStr);
        
        std::string mode = response.substr(colon2 + 1, response.find('\n') - colon2 - 1);
        bool compressed = (mode == "COMPRESSED");
        
        std::string expectedHash;
        for (const auto& file : availableFiles) {
            if (file.filename == filename) {
                expectedHash = file.sha256;
                break;
            }
        }
        
        resumeInfo.filename = filename;
        resumeInfo.expectedHash = expectedHash;
        resumeInfo.totalSize = offset + remainingSize;
        resumeInfo.bytesDownloaded = offset;
        resumeInfo.serverIP = serverIP;
        resumeInfo.serverPort = serverPort;
        
        std::ios::openmode openMode = std::ios::binary;
        openMode |= (offset > 0) ? std::ios::app : std::ios::trunc;
        
        std::ofstream outFile(savePath, openMode);
        if (!outFile) {
            std::cerr << "ERROR: Cannot create file\n";
            closesocket(sock);
            return false;
        }
        
        std::cout << "\nDownloading " << filename << "...\n";
        
        auto startTime = std::chrono::steady_clock::now();
        size_t totalReceived = offset;
        size_t bytesToReceive = remainingSize;
        size_t totalSize = offset + remainingSize;
        
        char recvBuffer[CHUNK_SIZE];
        bool downloadComplete = false;
        
        try {
            if (compressed) {
                while (bytesToReceive > 0) {
                    uint32_t compressedSize;
                    int headerBytes = recv(sock, (char*)&compressedSize, sizeof(compressedSize), MSG_WAITALL);
                    if (headerBytes <= 0) break;
                    
                    size_t received = 0;
                    while (received < compressedSize) {
                        int n = recv(sock, recvBuffer + received, 
                                   std::min((size_t)CHUNK_SIZE - received, compressedSize - received), 0);
                        if (n <= 0) break;
                        received += n;
                    }
                    
                    if (received < compressedSize) break;
                    
                    std::vector<char> decompressed = decompressData(recvBuffer, received, CHUNK_SIZE);
                    if (decompressed.empty()) break;
                    
                    outFile.write(decompressed.data(), decompressed.size());
                    totalReceived += decompressed.size();
                    bytesToReceive = (bytesToReceive >= decompressed.size()) ? 
                                    bytesToReceive - decompressed.size() : 0;
                    
                    showProgress(totalReceived, totalSize, startTime);
                }
            } else {
                while (bytesToReceive > 0) {
                    int n = recv(sock, recvBuffer, std::min((size_t)CHUNK_SIZE, bytesToReceive), 0);
                    if (n <= 0) break;
                    
                    outFile.write(recvBuffer, n);
                    outFile.flush();
                    
                    totalReceived += n;
                    bytesToReceive -= n;
                    
                    if (!compressed && (totalReceived % (1024 * 1024) == 0 || bytesToReceive == 0)) {
                        resumeInfo.bytesDownloaded = totalReceived;
                        resumeInfo.save(savePath);
                    }
                    
                    showProgress(totalReceived, totalSize, startTime);
                }
            }
            
            downloadComplete = (bytesToReceive == 0);
            
        } catch (...) {
            std::cout << "\n";
            outFile.close();
            closesocket(sock);
            
            if (!compressed) {
                resumeInfo.bytesDownloaded = totalReceived;
                resumeInfo.save(savePath);
                std::cout << ANSI_YELLOW << "Download interrupted. Resume info saved.\n" << ANSI_RESET;
                std::cout << "Run the download again to resume from " << formatSize(totalReceived) << "\n";
            }
            return false;
        }
        
        std::cout << "\n";
        outFile.close();
        closesocket(sock);
        
        if (!downloadComplete) {
            std::cerr << ANSI_YELLOW << "WARNING: Download incomplete (" 
                      << formatSize(bytesToReceive) << " remaining)\n" << ANSI_RESET;
            
            if (!compressed) {
                resumeInfo.bytesDownloaded = totalReceived;
                resumeInfo.save(savePath);
                std::cout << "Partial file saved. Run download again to resume.\n";
            }
            return false;
        }
        
        if (!expectedHash.empty()) {
            if (!verifyChecksum(savePath, expectedHash)) {
                std::cout << ANSI_YELLOW << "WARNING: Checksum mismatch! File may be corrupted.\n" << ANSI_RESET;
                
                try {
                    fs::remove(savePath);
                    resumeInfo.remove(savePath);
                } catch (...) {}
                
                return false;
            }
        }
        
        resumeInfo.remove(savePath);
        
        return true;
    }
    
    bool downloadByIndex(int index) {
        if (index < 0 || index >= (int)availableFiles.size()) return false;
        
        const FileEntry& file = availableFiles[index];
        fs::path downloadPath = fs::path(config.downloadFolder) / file.filename;
        return downloadFile(file.filename, downloadPath.string());
    }
    
    void setDownloadFolder(const std::string& folder) {
        config.downloadFolder = folder;
        config.save();
        
        try {
            if (!fs::exists(folder)) {
                fs::create_directories(folder);
            }
        } catch (...) {}
    }
    
    void toggleCompression() {
        config.enableCompression = !config.enableCompression;
        config.save();
        std::cout << "Compression " << (config.enableCompression ? "enabled" : "disabled") << "\n";
        if (config.enableCompression) {
            std::cout << ANSI_YELLOW << "Note: Resume functionality is disabled when compression is enabled.\n" << ANSI_RESET;
        }
    }
    
    std::string getServerIP() const { return serverIP; }
    int getServerPort() const { return serverPort; }
    bool isCompressionEnabled() const { return config.enableCompression; }
    std::string getDownloadFolder() const { return config.downloadFolder; }
};

void printBanner() {
    system("cls");
    std::cout << ANSI_CYAN;
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║           " << ANSI_GREEN << "FILE SHARING CLIENT v2.2" << ANSI_CYAN << "                 ║\n";
    std::cout << "║        " << ANSI_YELLOW << "Resume | Compression | Checksums" << ANSI_CYAN << "             ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << ANSI_RESET << "\n";
}

int main(int argc, char* argv[]) {
    FileClient client;
    
    if (argc >= 3) {
        client.setServer(argv[1], std::stoi(argv[2]));
    }
    
    bool running = true;
    
    while (running) {
        printBanner();
        
        if (!client.getServerIP().empty()) {
            std::cout << ANSI_GREEN << "  Connected to: " << ANSI_RESET 
                      << client.getServerIP() << ":" << client.getServerPort() << "\n";
        } else {
            std::cout << ANSI_YELLOW << "  Not connected to any server" << ANSI_RESET << "\n";
        }
        
        std::cout << ANSI_GRAY << "  Download folder: " << client.getDownloadFolder() << "\n";
        std::cout << "  Compression: " << (client.isCompressionEnabled() ? "ON" : "OFF") 
                  << ANSI_RESET << "\n\n";
        
        Menu mainMenu("Main Menu");
        mainMenu.addItem("Connect to Server", "Enter server IP and port");
        mainMenu.addItem("Browse Files", "View and download available files");
        mainMenu.addItem("Settings", "Configure client settings");
        mainMenu.addItem("Exit", "Quit the application");
        
        int choice = mainMenu.show();
        
        if (choice == -1 || choice == 3) {
            if (confirmDialog("Are you sure you want to exit?")) {
                running = false;
            }
        }
        else if (choice == 0) {
            system("cls");
            std::cout << ANSI_CYAN << "\n╔════════════════════════════════════╗\n";
            std::cout << "║         Connect to Server         ║\n";
            std::cout << "╚════════════════════════════════════╝\n" << ANSI_RESET << "\n";
            
            std::cout << "Server IP: ";
            std::string ip;
            std::getline(std::cin, ip);
            
            std::cout << "Port [8080]: ";
            std::string portStr;
            std::getline(std::cin, portStr);
            int port = portStr.empty() ? 8080 : std::stoi(portStr);
            
            client.setServer(ip, port);
            
            std::cout << "\nTesting connection... " << std::flush;
            if (client.testConnection()) {
                std::cout << ANSI_GREEN << "Success!" << ANSI_RESET << "\n";
            } else {
                std::cout << ANSI_YELLOW << "Failed!" << ANSI_RESET << "\n";
            }
            std::cout << "\nPress any key to continue...";
            _getch();
        }
        else if (choice == 1) {
            if (client.getServerIP().empty()) {
                system("cls");
                std::cout << ANSI_YELLOW << "\nPlease connect to a server first!\n" << ANSI_RESET;
                std::cout << "\nPress any key to continue...";
                _getch();
                continue;
            }
            
            system("cls");
            std::cout << ANSI_CYAN << "Fetching file list...\n" << ANSI_RESET;
            
            if (!client.listFiles()) {
                std::cout << ANSI_YELLOW << "\nFailed to retrieve file list.\n" << ANSI_RESET;
                std::cout << "\nPress any key to continue...";
                _getch();
                continue;
            }
            
            int fileIndex = client.showFileMenu();
            
            if (fileIndex >= 0) {
                if (confirmDialog("Download this file?")) {
                    system("cls");
                    std::cout << ANSI_CYAN << "\n╔════════════════════════════════════╗\n";
                    std::cout << "║            Downloading            ║\n";
                    std::cout << "╚════════════════════════════════════╝\n" << ANSI_RESET << "\n";
                    
                    if (client.downloadByIndex(fileIndex)) {
                        std::cout << "\n" << ANSI_GREEN << "Download complete!" << ANSI_RESET << "\n";
                    } else {
                        std::cout << "\n" << ANSI_YELLOW << "Download failed or incomplete!" << ANSI_RESET << "\n";
                    }
                    
                    std::cout << "\nPress any key to continue...";
                    _getch();
                }
            }
        }
        else if (choice == 2) {
            bool inSettings = true;
            
            while (inSettings) {
                system("cls");
                std::cout << ANSI_CYAN << "\n╔════════════════════════════════════╗\n";
                std::cout << "║             Settings              ║\n";
                std::cout << "╚════════════════════════════════════╝\n" << ANSI_RESET << "\n";
                
                std::cout << "Current Settings:\n";
                std::cout << "  Server: " << (client.getServerIP().empty() ? "Not set" : 
                             client.getServerIP() + ":" + std::to_string(client.getServerPort())) << "\n";
                std::cout << "  Download Folder: " << client.getDownloadFolder() << "\n";
                std::cout << "  Compression: " << (client.isCompressionEnabled() ? "ON" : "OFF") << "\n\n";
                
                Menu settingsMenu("Settings");
                settingsMenu.addItem("Change Download Folder", "Set where files are saved");
                settingsMenu.addItem("Toggle Compression", 
                                   client.isCompressionEnabled() ? "Currently: ON" : "Currently: OFF");
                settingsMenu.addItem("Back to Main Menu", "Return to main menu");
                
                int settingChoice = settingsMenu.show();
                
                if (settingChoice == -1 || settingChoice == 2) {
                    inSettings = false;
                }
                else if (settingChoice == 0) {
                    system("cls");
                    std::cout << ANSI_CYAN << "\n╔════════════════════════════════════╗\n";
                    std::cout << "║       Change Download Folder      ║\n";
                    std::cout << "╚════════════════════════════════════╝\n" << ANSI_RESET << "\n";
                    
                    std::cout << "Current folder: " << client.getDownloadFolder() << "\n\n";
                    std::cout << "New download folder: ";
                    std::string folder;
                    std::getline(std::cin, folder);
                    
                    if (!folder.empty()) {
                        client.setDownloadFolder(folder);
                        std::cout << ANSI_GREEN << "\nFolder updated!" << ANSI_RESET << "\n";
                    }
                    
                    std::cout << "\nPress any key to continue...";
                    _getch();
                }
                else if (settingChoice == 1) {
                    client.toggleCompression();
                    std::cout << "\nPress any key to continue...";
                    _getch();
                }
            }
        }
    }
    
    system("cls");
    std::cout << ANSI_GREEN << "\nThank you for using File Sharing Client!\n" << ANSI_RESET;
    
    return 0;
}