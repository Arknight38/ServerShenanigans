#ifndef MENU_H
#define MENU_H

#include <iostream>
#include <string>
#include <vector>
#include <conio.h>
#include <windows.h>
#include <algorithm>

// ANSI color codes for Windows
#define ANSI_RESET "\033[0m"
#define ANSI_HIGHLIGHT "\033[7m"  // Inverted colors
#define ANSI_GRAY "\033[90m"
#define ANSI_GREEN "\033[92m"
#define ANSI_CYAN "\033[96m"
#define ANSI_YELLOW "\033[93m"

// Arrow key codes
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_ENTER 13
#define KEY_ESC 27
#define KEY_BACKSPACE 8

class Menu {
private:
    std::vector<std::string> items;
    std::vector<std::string> descriptions;
    int selected;
    int scrollOffset;
    int maxVisible;
    std::string title;
    std::string searchQuery;
    bool searchMode;
    std::vector<int> filteredIndices;
    
    void enableANSI() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
        
        // Add UTF-8 support
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
    
    void clearScreen() {
        system("cls");
    }
    
    void moveCursor(int x, int y) {
        COORD coord;
        coord.X = x;
        coord.Y = y;
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
    }
    
    void hideCursor() {
        HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 100;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(consoleHandle, &info);
    }
    
    void showCursor() {
        HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 100;
        info.bVisible = TRUE;
        SetConsoleCursorInfo(consoleHandle, &info);
    }
    
    void updateFilteredIndices() {
        filteredIndices.clear();
        
        if (searchQuery.empty()) {
            for (size_t i = 0; i < items.size(); i++) {
                filteredIndices.push_back((int)i);
            }
        } else {
            std::string lowerQuery = searchQuery;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
            
            for (size_t i = 0; i < items.size(); i++) {
                std::string lowerItem = items[i];
                std::transform(lowerItem.begin(), lowerItem.end(), lowerItem.begin(), ::tolower);
                
                if (lowerItem.find(lowerQuery) != std::string::npos) {
                    filteredIndices.push_back((int)i);
                }
            }
        }
        
        // Reset selection if out of bounds
        if (selected >= (int)filteredIndices.size()) {
            selected = std::max(0, (int)filteredIndices.size() - 1);
        }
    }
    
    void render() {
        clearScreen();
        hideCursor();
        
        // Draw title
        std::cout << ANSI_CYAN << "╔";
        for (int i = 0; i < 60; i++) std::cout << "═";
        std::cout << "╗" << ANSI_RESET << "\n";
        
        std::cout << ANSI_CYAN << "║ " << ANSI_RESET 
                  << ANSI_GREEN << title << ANSI_RESET;
        int padding = 58 - (int)title.length();
        for (int i = 0; i < padding; i++) std::cout << " ";
        std::cout << ANSI_CYAN << " ║" << ANSI_RESET << "\n";
        
        std::cout << ANSI_CYAN << "╚";
        for (int i = 0; i < 60; i++) std::cout << "═";
        std::cout << "╝" << ANSI_RESET << "\n\n";
        
        // Search bar
        if (searchMode || !searchQuery.empty()) {
            std::cout << ANSI_YELLOW << "Search: " << ANSI_RESET 
                      << searchQuery;
            if (searchMode) std::cout << "▌";
            std::cout << "\n";
            std::cout << ANSI_GRAY << "   " << filteredIndices.size() 
                      << " of " << items.size() << " items" << ANSI_RESET << "\n\n";
        }
        
        // Calculate visible range
        if (selected < scrollOffset) {
            scrollOffset = selected;
        }
        if (selected >= scrollOffset + maxVisible) {
            scrollOffset = selected - maxVisible + 1;
        }
        
        // Draw items
        int endIdx = std::min(scrollOffset + maxVisible, (int)filteredIndices.size());
        
        for (int i = scrollOffset; i < endIdx; i++) {
            int actualIdx = filteredIndices[i];
            bool isSelected = (i == selected);
            
            if (isSelected) {
                std::cout << ANSI_HIGHLIGHT << " ► ";
            } else {
                std::cout << "   ";
            }
            
            std::cout << items[actualIdx];
            
            if (isSelected) {
                std::cout << ANSI_RESET;
            }
            
            std::cout << "\n";
            
            // Show description if available and selected
            if (isSelected && actualIdx < (int)descriptions.size() && 
                !descriptions[actualIdx].empty()) {
                std::cout << ANSI_GRAY << "     " << descriptions[actualIdx] 
                          << ANSI_RESET << "\n";
            }
        }
        
        // Scroll indicators
        if (scrollOffset > 0) {
            moveCursor(0, 5);
            std::cout << ANSI_GRAY << "     ▲ More items above" << ANSI_RESET;
        }
        if (endIdx < (int)filteredIndices.size()) {
            std::cout << "\n" << ANSI_GRAY << "     ▼ More items below" << ANSI_RESET;
        }
        
        // Controls hint
        std::cout << "\n\n" << ANSI_GRAY;
        std::cout << "  [↑↓] Navigate  [Enter] Select  [/] Search  ";
        std::cout << "[Esc] " << (searchMode ? "Cancel Search" : "Exit");
        std::cout << ANSI_RESET << "\n";
    }
    
public:
    Menu(const std::string& t, int maxVis = 15) 
        : title(t), selected(0), scrollOffset(0), 
          maxVisible(maxVis), searchMode(false) {
        enableANSI();
    }
    
    void addItem(const std::string& item, const std::string& desc = "") {
        items.push_back(item);
        descriptions.push_back(desc);
        updateFilteredIndices();
    }
    
    void setItems(const std::vector<std::string>& newItems, 
                  const std::vector<std::string>& newDescs = {}) {
        items = newItems;
        descriptions = newDescs;
        if (descriptions.size() < items.size()) {
            descriptions.resize(items.size());
        }
        selected = 0;
        scrollOffset = 0;
        updateFilteredIndices();
    }
    
    int show() {
        if (items.empty()) {
            std::cout << "No items to display.\n";
            return -1;
        }
        
        updateFilteredIndices();
        
        while (true) {
            render();
            
            int key = _getch();
            
            // Handle extended keys (arrows)
            if (key == 0 || key == 224) {
                key = _getch();
                
                switch (key) {
                    case KEY_UP:
                        if (selected > 0) selected--;
                        break;
                    case KEY_DOWN:
                        if (selected < (int)filteredIndices.size() - 1) selected++;
                        break;
                }
            }
            // Regular keys
            else {
                switch (key) {
                    case KEY_ENTER:
                        showCursor();
                        clearScreen();
                        if (!filteredIndices.empty()) {
                            return filteredIndices[selected];
                        }
                        return -1;
                        
                    case KEY_ESC:
                        if (searchMode || !searchQuery.empty()) {
                            searchMode = false;
                            searchQuery.clear();
                            updateFilteredIndices();
                            selected = 0;
                        } else {
                            showCursor();
                            clearScreen();
                            return -1;
                        }
                        break;
                        
                    case '/':
                        searchMode = true;
                        break;
                        
                    case KEY_BACKSPACE:
                        if (searchMode && !searchQuery.empty()) {
                            searchQuery.pop_back();
                            updateFilteredIndices();
                        }
                        break;
                        
                    default:
                        // Printable characters for search
                        if (searchMode && key >= 32 && key <= 126) {
                            searchQuery += (char)key;
                            updateFilteredIndices();
                            selected = 0;
                        }
                        break;
                }
            }
        }
    }
    
    void clear() {
        items.clear();
        descriptions.clear();
        selected = 0;
        scrollOffset = 0;
        searchQuery.clear();
        searchMode = false;
    }
};

// Quick confirmation menu
bool confirmDialog(const std::string& question) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    system("cls");
    
    std::cout << ANSI_YELLOW << "\n  " << question << ANSI_RESET << "\n\n";
    
    int selected = 0;
    std::vector<std::string> options = {"Yes", "No"};
    
    while (true) {
        for (int i = 0; i < 2; i++) {
            if (i == selected) {
                std::cout << ANSI_HIGHLIGHT << " ► " << options[i] << " " << ANSI_RESET;
            } else {
                std::cout << "   " << options[i] << "  ";
            }
        }
        std::cout << "\r" << std::flush;
        
        int key = _getch();
        
        if (key == 0 || key == 224) {
            key = _getch();
            if (key == KEY_LEFT) selected = 0;
            if (key == KEY_RIGHT) selected = 1;
        } else if (key == KEY_ENTER) {
            system("cls");
            return selected == 0;
        } else if (key == KEY_ESC) {
            system("cls");
            return false;
        }
    }
}

#endif // MENU_H