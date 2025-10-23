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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "libcrypto.lib")

namespace fs = std::filesystem;

const int CHUNK_SIZE = 65536; // 64KB
const std::string CONFIG_FILE = "client_config.txt";
const std::string RESUME_DIR = ".resume";

struct FileEntry {
    std::string filename;
    size_t filesize;
    std::string sha256;
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
    
    std::vector<char> decompressData(const char* data, size_t compressedSize, 
                                    size_t originalSize) {
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
    
    void showProgress(size_t current, size_t total, 
                     std::chrono::steady_clock::time_point startTime) {
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
        
        // Ensure download folder exists
        if (config.downloadFolder.empty() || config.downloadFolder == ".") {
            config.downloadFolder = fs::current_path().string();
        }
        
        // Create directories
        try {
            fs::create_directories(config.downloadFolder);
            fs::create_directories(RESUME_DIR);
        } catch (...) {
            // Ignore errors, will be caught during download
        }
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
                
                // Format: filename:size:sha256
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
    
    void displayFiles() {
        if (availableFiles.empty()) {
            std::cout << "\nNo files available.\n";
            return;
        }
        
        std::cout << "\n========================================\n";
        std::cout << "Available Files (" << availableFiles.size() << " total)\n";
        std::cout << "========================================\n";
        
        for (size_t i = 0; i < availableFiles.size(); i++) {
            std::cout << "[" << (i + 1) << "] " << availableFiles[i].filename;
            
            double size = availableFiles[i].filesize;
            if (size < 1024) {
                std::cout << " (" << size << " B)";
            } else if (size < 1024 * 1024) {
                std::cout << " (" << std::fixed << std::setprecision(2) 
                         << (size / 1024.0) << " KB)";
            } else {
                std::cout << " (" << std::fixed << std::setprecision(2) 
                         << (size / (1024.0 * 1024.0)) << " MB)";
            }
            
            std::cout << "\n    SHA256: " << availableFiles[i].sha256.substr(0, 16) << "...\n";
        }
        
        std::cout << "========================================\n";
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
    
    bool downloadFile(const std::string& filename, const std::string& savePath, 
                     bool resume = true) {
        if (!wsaInitialized) {
            std::cerr << "ERROR: Winsock not initialized\n";
            return false;
        }
        
        // Check if partial file exists
        size_t offset = 0;
        
        // Disable resume when compression is enabled
        if (config.enableCompression) {
            resume = false;
            // Delete partial file if it exists
            if (fs::exists(savePath)) {
                try {
                    fs::remove(savePath);
                    std::cout << "\nNote: Removed partial file (compression enabled, cannot resume)\n";
                } catch (...) {
                    std::cerr << "Warning: Could not remove partial file\n";
                }
            }
        } else if (resume && fs::exists(savePath)) {
            offset = getFileSize(savePath);
            if (offset > 0) {
                std::cout << "\nResuming download from " << offset << " bytes...\n";
            }
        }
        
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "ERROR: Socket creation failed: " << WSAGetLastError() << "\n";
            return false;
        }
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        
        if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "ERROR: Invalid IP address: " << serverIP << "\n";
            closesocket(sock);
            return false;
        }
        
        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "ERROR: Connection failed: " << WSAGetLastError() << "\n";
            std::cerr << "       Make sure server is running at " << serverIP << ":" << serverPort << "\n";
            closesocket(sock);
            return false;
        }
        
        // Build request with resume support
        std::stringstream request;
        request << "GET " << filename;
        if (offset > 0) {
            request << " OFFSET " << offset;
        }
        if (config.enableCompression) {
            request << " COMPRESS";
        }
        
        std::string reqStr = request.str();
        std::cout << "Sending request: " << reqStr << "\n";
        
        int sentBytes = send(sock, reqStr.c_str(), (int)reqStr.length(), 0);
        if (sentBytes == SOCKET_ERROR) {
            std::cerr << "ERROR: Failed to send request: " << WSAGetLastError() << "\n";
            closesocket(sock);
            return false;
        }
        
        // Receive response header
        char buffer[1024];
        int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) {
            std::cerr << "ERROR: No response from server (recv returned " << bytesRead << ")\n";
            std::cerr << "       Error code: " << WSAGetLastError() << "\n";
            closesocket(sock);
            return false;
        }
        
        buffer[bytesRead] = '\0';
        std::string response(buffer);
        
        std::cout << "Server response: " << response.substr(0, 50) << "...\n";
        
        if (response.find("ERROR") == 0) {
            std::cerr << "Server error: " << response;
            closesocket(sock);
            return false;
        }
        
        // Parse: OK:size:mode
        if (response.find("OK:") != 0) {
            std::cerr << "ERROR: Unexpected response format: " << response.substr(0, 30) << "\n";
            closesocket(sock);
            return false;
        }
        
        size_t colon1 = response.find(':');
        size_t colon2 = response.find(':', colon1 + 1);
        size_t newline = response.find('\n');
        
        if (colon1 == std::string::npos || colon2 == std::string::npos) {
            std::cerr << "ERROR: Failed to parse response header\n";
            closesocket(sock);
            return false;
        }
        
        std::string sizeStr = response.substr(colon1 + 1, colon2 - colon1 - 1);
        size_t remainingSize = 0;
        try {
            remainingSize = std::stoull(sizeStr);
        } catch (...) {
            std::cerr << "ERROR: Invalid file size in response: " << sizeStr << "\n";
            closesocket(sock);
            return false;
        }
        
        std::string mode = response.substr(colon2 + 1, newline - colon2 - 1);
        bool compressed = (mode == "COMPRESSED");
        
        std::cout << "File size: " << remainingSize << " bytes, Mode: " << mode << "\n";
        
        // Open file for writing
        std::ios::openmode openMode = std::ios::binary;
        if (offset > 0) {
            openMode |= std::ios::app;
        } else {
            openMode |= std::ios::trunc;
        }
        
        std::ofstream outFile(savePath, openMode);
        if (!outFile) {
            std::cerr << "ERROR: Cannot create file: " << savePath << "\n";
            std::cerr << "       Check folder permissions and disk space\n";
            closesocket(sock);
            return false;
        }
        
        std::cout << "\nDownloading " << filename << " (" 
                  << (compressed ? "compressed" : "raw") << ")...\n";
        
        auto startTime = std::chrono::steady_clock::now();
        size_t totalReceived = offset;
        size_t bytesToReceive = remainingSize;
        size_t totalSize = offset + remainingSize;
        
        char recvBuffer[CHUNK_SIZE];
        
        try {
            if (compressed) {
                while (bytesToReceive > 0) {
                    // Receive compressed chunk size
                    uint32_t compressedSize;
                    int sizeRead = recv(sock, (char*)&compressedSize, sizeof(compressedSize), MSG_WAITALL);
                    if (sizeRead <= 0) {
                        std::cerr << "\nERROR: Connection lost while receiving chunk size\n";
                        break;
                    }
                    
                    // Receive compressed data
                    size_t received = 0;
                    while (received < compressedSize) {
                        int n = recv(sock, recvBuffer + received, 
                                   std::min((size_t)CHUNK_SIZE - received, compressedSize - received), 0);
                        if (n <= 0) {
                            std::cerr << "\nERROR: Connection lost while receiving data\n";
                            break;
                        }
                        received += n;
                    }
                    
                    if (received < compressedSize) break;
                    
                    // Decompress
                    std::vector<char> decompressed = decompressData(recvBuffer, received, CHUNK_SIZE);
                    if (decompressed.empty()) {
                        std::cerr << "\nERROR: Decompression failed\n";
                        break;
                    }
                    
                    outFile.write(decompressed.data(), decompressed.size());
                    if (!outFile) {
                        std::cerr << "\nERROR: Failed to write to file\n";
                        break;
                    }
                    
                    totalReceived += decompressed.size();
                    
                    // For compressed mode, decrement based on decompressed size
                    if (bytesToReceive >= decompressed.size()) {
                        bytesToReceive -= decompressed.size();
                    } else {
                        bytesToReceive = 0;
                    }
                    
                    showProgress(totalReceived, totalSize, startTime);
                }
            } else {
                while (bytesToReceive > 0) {
                    int n = recv(sock, recvBuffer, 
                               std::min((size_t)CHUNK_SIZE, bytesToReceive), 0);
                    if (n <= 0) {
                        if (n == 0) {
                            std::cerr << "\nWARNING: Connection closed by server\n";
                        } else {
                            std::cerr << "\nERROR: Receive failed: " << WSAGetLastError() << "\n";
                        }
                        break;
                    }
                    
                    outFile.write(recvBuffer, n);
                    if (!outFile) {
                        std::cerr << "\nERROR: Failed to write to file\n";
                        break;
                    }
                    
                    totalReceived += n;
                    bytesToReceive -= n;
                    
                    showProgress(totalReceived, totalSize, startTime);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "\nEXCEPTION: " << e.what() << "\n";
            outFile.close();
            closesocket(sock);
            return false;
        }
        
        std::cout << "\n";
        
        outFile.close();
        closesocket(sock);
        
        if (bytesToReceive > 0) {
            std::cerr << "WARNING: Download incomplete (" << bytesToReceive << " bytes remaining)\n";
            if (!compressed) {
                std::cerr << "         Run download again to resume\n";
            } else {
                std::cerr << "         Compression is enabled - cannot resume. Try again from start.\n";
            }
            return false;
        }
        
        std::cout << "Downloaded: " << totalReceived << " bytes\n";
        std::cout << "Saved to: " << savePath << "\n";
        
        // Verify checksum if available
        for (const auto& file : availableFiles) {
            if (file.filename == filename && !file.sha256.empty()) {
                if (!verifyChecksum(savePath, file.sha256)) {
                    std::cout << "WARNING: Checksum mismatch! File may be corrupted.\n";
                    return false;
                }
                break;
            }
        }
        
        return true;
    }
    
    bool downloadByIndex(int index) {
        if (index < 1 || index > (int)availableFiles.size()) {
            return false;
        }
        
        const FileEntry& file = availableFiles[index - 1];
        
        // Use filesystem to properly construct path
        fs::path downloadPath = fs::path(config.downloadFolder) / file.filename;
        return downloadFile(file.filename, downloadPath.string());
    }
    
    void setDownloadFolder(const std::string& folder) {
        config.downloadFolder = folder;
        config.save();
        
        // Create directory if it doesn't exist
        try {
            if (!fs::exists(folder)) {
                fs::create_directories(folder);
                std::cout << "Created download folder: " << folder << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not create folder: " << e.what() << "\n";
        }
    }
    
    void toggleCompression() {
        config.enableCompression = !config.enableCompression;
        config.save();
        std::cout << "Compression " 
                  << (config.enableCompression ? "enabled" : "disabled") << "\n";
        if (config.enableCompression) {
            std::cout << "Note: Resume is disabled when compression is enabled.\n";
        }
    }
    
    std::string getServerIP() const { return serverIP; }
    int getServerPort() const { return serverPort; }
    bool isCompressionEnabled() const { return config.enableCompression; }
};

void printBanner() {
    std::cout << "\n========================================\n";
    std::cout << "    FILE SHARING CLIENT v2.0\n";
    std::cout << "  Resume | Compression | Checksums\n";
    std::cout << "========================================\n";
}

void printMenu() {
    std::cout << "\n--- MENU ---\n";
    std::cout << "1. Connect to server\n";
    std::cout << "2. List available files\n";
    std::cout << "3. Download file by number\n";
    std::cout << "4. Download file by name\n";
    std::cout << "5. Set download folder\n";
    std::cout << "6. Toggle compression\n";
    std::cout << "7. Show settings\n";
    std::cout << "8. Exit\n";
    std::cout << "\nChoice: ";
}

int main(int argc, char* argv[]) {
    FileClient client;
    printBanner();
    
    if (argc >= 3) {
        client.setServer(argv[1], std::stoi(argv[2]));
        std::cout << "Server: " << argv[1] << ":" << argv[2] << "\n";
        
        std::cout << "Testing... ";
        if (client.testConnection()) {
            std::cout << "Connected!\n";
        } else {
            std::cout << "Failed.\n";
        }
    } else if (!client.getServerIP().empty()) {
        std::cout << "Last server: " << client.getServerIP() 
                  << ":" << client.getServerPort() << "\n";
    }
    
    bool running = true;
    
    while (running) {
        printMenu();
        
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        switch (choice) {
            case 1: {
                std::cout << "\nServer IP: ";
                std::string ip;
                std::getline(std::cin, ip);
                
                std::cout << "Port [8080]: ";
                std::string portStr;
                std::getline(std::cin, portStr);
                int port = portStr.empty() ? 8080 : std::stoi(portStr);
                
                client.setServer(ip, port);
                
                std::cout << "\nTesting... ";
                if (client.testConnection()) {
                    std::cout << "Success!\n";
                } else {
                    std::cout << "Failed!\n";
                }
                break;
            }
            
            case 2: {
                if (client.getServerIP().empty()) {
                    std::cout << "\nConnect to server first!\n";
                    break;
                }
                
                std::cout << "\nRefreshing...\n";
                if (client.listFiles()) {
                    client.displayFiles();
                } else {
                    std::cout << "Failed to get file list.\n";
                }
                break;
            }
            
            case 3: {
                std::cout << "\nFile number: ";
                int num;
                std::cin >> num;
                std::cin.ignore();
                
                if (client.downloadByIndex(num)) {
                    std::cout << "\nDownload complete!\n";
                } else {
                    std::cout << "\nDownload failed.\n";
                }
                break;
            }
            
            case 4: {
                std::cout << "\nFilename: ";
                std::string filename;
                std::getline(std::cin, filename);
                
                std::cout << "Save as [" << filename << "]: ";
                std::string saveName;
                std::getline(std::cin, saveName);
                if (saveName.empty()) saveName = filename;
                
                if (client.downloadFile(filename, saveName)) {
                    std::cout << "\nDownload complete!\n";
                } else {
                    std::cout << "\nDownload failed.\n";
                }
                break;
            }
            
            case 5: {
                std::cout << "\nDownload folder: ";
                std::string folder;
                std::getline(std::cin, folder);
                client.setDownloadFolder(folder);
                std::cout << "Folder set to: " << folder << "\n";
                break;
            }
            
            case 6: {
                client.toggleCompression();
                break;
            }
            
            case 7: {
                std::cout << "\n--- SETTINGS ---\n";
                std::cout << "Server: " << client.getServerIP() 
                         << ":" << client.getServerPort() << "\n";
                std::cout << "Compression: " 
                         << (client.isCompressionEnabled() ? "ON" : "OFF") << "\n";
                break;
            }
            
            case 8: {
                std::cout << "\nGoodbye!\n";
                running = false;
                break;
            }
            
            default:
                std::cout << "\nInvalid choice.\n";
        }
    }
    
    return 0;
}