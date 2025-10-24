# File Sharing System

A lightweight, high-performance peer-to-peer file sharing system built in C++ with resume capability, compression, and integrity verification.

## Features

### Client Features
- **Resume Downloads** - Automatically resume interrupted downloads from where they left off
- **Compression Support** - Optional zlib compression for faster transfers over slow connections
- **Integrity Verification** - SHA-256 checksums ensure file integrity
- **Progress Tracking** - Real-time download progress with speed indicators
- **Persistent Configuration** - Remembers server settings and preferences
- **Configurable Download Folder** - Choose where to save downloaded files

### Server Features
- **Multi-threaded** - Handle up to 50 concurrent connections
- **Auto-folder Sharing** - Automatically share all files in a folder
- **Tab Completion** - Intelligent path completion for file and folder operations
- **Compression Control** - Enable/disable compression server-wide
- **SHA-256 Hashing** - Automatic checksum calculation for all shared files
- **Resume Support** - Supports partial file transfers with offset handling

## Requirements

- **Windows OS** (uses Winsock2)
- **MinGW-w64** with g++ compiler
- **vcpkg** (automatically installed by build script)
- **Git** (for vcpkg installation)

### Dependencies (automatically installed via vcpkg)
- OpenSSL (for SHA-256 hashing)
- zlib (for compression)

## Quick Start

### Building

Simply run the build script:

```batch
build.bat
```

The script will:
1. Check for or install vcpkg
2. Install required dependencies (OpenSSL, zlib)
3. Compile both client and server
4. Copy necessary DLLs to the builds directory

### Running the Server

```batch
cd builds
server.exe
```

The server will start on port 8080 by default and display your local IP address.

#### Server Commands

```
add <filepath>           - Share a single file (supports TAB completion)
addfolder <path>         - Share all files in a folder (supports TAB completion)
remove <filename>        - Remove a file from sharing
list                     - Display all shared files
setfolder <path>         - Set folder to auto-share on startup
compress on/off          - Toggle compression
quit                     - Exit server
```

**Example:**
```
> add C:\Documents\file.pdf
> addfolder C:\SharedFiles
> list
> compress on
```

### Running the Client

```batch
cd builds
client.exe [server_ip] [port]
```

Or simply run `client.exe` and configure interactively.

#### Client Menu

```
1. Connect to server      - Set server IP and port
2. List available files   - Refresh file list from server
3. Download by number     - Download using file list number
4. Download by name       - Download specific file
5. Set download folder    - Change download location
6. Toggle compression     - Enable/disable compression
7. Show settings          - Display current configuration
8. Exit                   - Quit client
```

## Configuration Files

### server_config.txt
```ini
# Server Configuration
port=8080
compression=true
max_connections=50
shared_folder=C:\SharedFiles
```

### client_config.txt
```ini
# Client Configuration
server=192.168.1.100
port=8080
compression=true
download_folder=C:\Downloads
```

Both files are automatically created and updated through the application.

## Protocol Details

### Commands

**LIST** - Request list of available files
```
Client: LIST
Server: filename1:size1:sha256_1\nfilename2:size2:sha256_2\n...
```

**GET** - Download a file (with optional resume and compression)
```
Client: GET filename [OFFSET bytes] [COMPRESS]
Server: OK:remaining_size:MODE\n[file data]
```

**CHECKSUM** - Request file checksum
```
Client: CHECKSUM filename
Server: CHECKSUM:sha256_hash\n
```

### Transfer Modes

**RAW Mode** - Direct file transfer
- Efficient for fast networks
- Supports resume via OFFSET parameter
- Simple byte-by-byte transfer

**COMPRESSED Mode** - Compressed transfer
- Better for slow connections
- Each chunk is compressed separately
- 4-byte size header precedes each compressed chunk
- Resume not supported (starts from beginning)

## Performance

- **Chunk Size:** 64KB for optimal balance between memory and speed
- **Compression:** zlib with `Z_BEST_SPEED` for low CPU overhead
- **Threading:** One thread per client connection
- **Buffer Management:** Stack-allocated buffers for minimal heap allocation

## Resume Capability

The client automatically detects partially downloaded files and resumes from the last byte received:

```
> Download interrupted at 45% (23 MB / 50 MB)
> Restart client and select same file
> Automatically resumes from 23 MB
```

**Note:** Resume is disabled when compression is enabled. The client will notify you and restart from the beginning.

## Security Considerations

⚠️ **This is a local network file sharing tool. Important notes:**

- No authentication or encryption
- Designed for trusted networks only
- All files are accessible to anyone who can connect
- SHA-256 checksums verify integrity but not authenticity
- Use behind a firewall, never expose to the internet

## Examples

### Example 1: Basic File Sharing

**Server:**
```
> add myfile.zip
[HASHING] myfile.zip... Done
[SHARED] myfile.zip (1048576 bytes)
```

**Client:**
```
Choice: 2  (List files)
[1] myfile.zip (1.00 MB)
    SHA256: a3f5d8c9b2e1...

Choice: 3  (Download by number)
File number: 1
[==============================>] 100.0% 5.23 MB/s
Downloaded: 1048576 bytes
Verifying checksum... OK
```

### Example 2: Folder Sharing with Compression

**Server:**
```
> addfolder C:\Documents
[HASHING] document1.pdf... Done
[HASHING] document2.docx... Done
[INFO] Added 2 files from folder

> compress on
Compression enabled.
```

**Client:**
```
Choice: 6  (Toggle compression)
Compression enabled
Note: Resume is disabled when compression is enabled.

Choice: 2  (List files)
[1] document1.pdf (2.34 MB)
[2] document2.docx (1.12 MB)

Choice: 3
File number: 1
[==============================>] 100.0% 3.45 MB/s
(Compressed transfer completed)
```

### Example 3: Resume Download

**Client (first attempt - interrupted):**
```
Choice: 3
File number: 1
[===============>              ] 52.3% 4.12 MB/s
ERROR: Connection lost
WARNING: Download incomplete (500000 bytes remaining)
         Run download again to resume
```

**Client (second attempt - resumed):**
```
Choice: 3
File number: 1
Resuming download from 548576 bytes...
[==============================>] 100.0% 4.05 MB/s
Downloaded: 1048576 bytes
Verifying checksum... OK
```

## Troubleshooting

### Build Issues

**Problem:** vcpkg fails to install
- Ensure Git is installed and in PATH
- Check internet connection
- Try deleting `vcpkg` folder and rebuilding

**Problem:** Compiler not found
- Install MinGW-w64
- Add MinGW `bin` directory to PATH
- Restart command prompt

### Connection Issues

**Problem:** Client cannot connect
- Verify server is running
- Check IP address (use `ipconfig` on server)
- Ensure firewall allows port 8080
- Try `localhost` if on same machine

**Problem:** "Server busy" error
- Server has reached max connections (50)
- Wait for other transfers to complete
- Increase `max_connections` in server_config.txt

### Download Issues

**Problem:** Checksum mismatch
- File may be corrupted on server
- Network error during transfer
- Try downloading again

**Problem:** Cannot write file
- Check disk space
- Verify folder permissions
- Ensure download folder exists

**Problem:** Resume not working
- Compression is enabled (disable to use resume)
- Partial file may be corrupted (delete and restart)
- Server file may have changed

## Technical Details

### Architecture

```
┌─────────────┐                  ┌─────────────┐
│   Client    │                  │   Server    │
│             │                  │             │
│ ┌─────────┐ │    TCP/IP        │ ┌─────────┐ │
│ │  Socket ├─┼──────────────────┼─┤ Listener│ │
│ └─────────┘ │                  │ └────┬────┘ │
│             │                  │      │      │
│ ┌─────────┐ │                  │ ┌────┴────┐ │
│ │Progress │ │                  │ │ Thread  │ │
│ │ Display │ │                  │ │  Pool   │ │
│ └─────────┘ │                  │ └────┬────┘ │
│             │                  │      │      │
│ ┌─────────┐ │                  │ ┌────┴────┐ │
│ │ SHA-256 │ │                  │ │  File   │ │
│ │ Verify  │ │                  │ │Manager  │ │
│ └─────────┘ │                  │ └─────────┘ │
└─────────────┘                  └─────────────┘
```

### Compression Algorithm

- **Algorithm:** DEFLATE (via zlib)
- **Level:** Z_BEST_SPEED (optimized for speed)
- **Chunk-based:** Each 64KB chunk compressed independently
- **Trade-off:** ~30-40% compression ratio, minimal CPU usage

### Memory Usage

- Server: ~2MB base + (500KB × active connections)
- Client: ~2MB base + (64KB × active downloads)
- All buffers are stack-allocated for performance

## Building from Source (Manual)

If you prefer manual building without the script:

```batch
# Install dependencies via vcpkg
vcpkg install openssl:x64-mingw-dynamic
vcpkg install zlib:x64-mingw-dynamic

# Build client
g++ -std=c++17 client.cpp -o client.exe ^
    -I"vcpkg/installed/x64-mingw-dynamic/include" ^
    -L"vcpkg/installed/x64-mingw-dynamic/lib" ^
    -lssl -lcrypto -lzlib -lws2_32

# Build server
g++ -std=c++17 server.cpp -o server.exe ^
    -I"vcpkg/installed/x64-mingw-dynamic/include" ^
    -L"vcpkg/installed/x64-mingw-dynamic/lib" ^
    -lssl -lcrypto -lzlib -lws2_32

# Copy DLLs
copy vcpkg\installed\x64-mingw-dynamic\bin\*.dll .
```

## License

This project is provided as-is for educational and personal use.

## Contributing

Contributions are welcome! Please ensure:
- Code follows existing style
- All features are tested on Windows
- Documentation is updated

## Future Enhancements

- [ ] Cross-platform support (Linux, macOS)
- [ ] Encryption support (TLS/SSL)
- [ ] User authentication
- [ ] Multiple simultaneous downloads
- [ ] File search/filtering

## Support

For issues, questions, or contributions, please open an issue on the repository.
Keep in mind that this is just me messing around :3

---

**Made with ❤️ for fast, reliable local file sharing**