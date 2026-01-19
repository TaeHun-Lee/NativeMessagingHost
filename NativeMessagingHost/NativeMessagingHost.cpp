#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#include <fcntl.h>
#include <psapi.h>      // GetModuleFileNameEx
#include <algorithm>    // transform
#include <cctype>       // ::tolower
#include <stdlib.h>     // __argc, __wargv

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

using namespace std;

// 로깅 헬퍼
void LogDebug(const wstring& msg) {
    OutputDebugStringW((L"[IME_HELPER] " + msg + L"\n").c_str());
}

// 내 프로세스의 부모 프로세스 ID(즉, Edge)를 찾는 함수
DWORD GetParentProcessId() {
    DWORD myPid = GetCurrentProcessId();
    DWORD parentPid = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == myPid) {
                    parentPid = pe32.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
    return parentPid;
}

// 특정 PID를 가진 프로세스의 메인 윈도우 핸들을 찾는 콜백 데이터
struct WindowSearchData {
    DWORD targetPid;
    HWND resultHwnd;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    WindowSearchData* data = reinterpret_cast<WindowSearchData*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    // 해당 프로세스의 윈도우이면서, 화면에 보이는 창만 찾음
    if (windowPid == data->targetPid && IsWindowVisible(hwnd)) {
        data->resultHwnd = hwnd;
        return FALSE; // 찾았으니 중단
    }
    return TRUE; // 계속 찾음
}

HWND FindWindowByProcessId(DWORD pid) {
    WindowSearchData data = { pid, NULL };
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));
    return data.resultHwnd;
}

// IME 변경 시도 함수
bool TrySetKoreanMode(HWND targetWindow) {
    HIMC hImc = ImmGetContext(targetWindow);
    if (hImc == NULL) return false;

    bool result = false;
    DWORD dwConversion, dwSentence;

    if (ImmGetConversionStatus(hImc, &dwConversion, &dwSentence)) {
        if ((dwConversion & IME_CMODE_HANGUL) == 0) {
            dwConversion |= IME_CMODE_HANGUL;
            if (ImmSetConversionStatus(hImc, dwConversion, dwSentence)) {
                LogDebug(L"Success: Switched to Korean Mode.");
                result = true;
            }
        }
        else {
            LogDebug(L"Info: Already Korean.");
            result = true;
        }
    }
    ImmReleaseContext(targetWindow, hImc);
    return result;
}

// PID로 프로세스 이름을 확인하여 Edge인지 판별하는 함수
bool IsEdgeProcess(DWORD pid) {
    if (pid == 0) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess == NULL) return false;

    WCHAR buffer[MAX_PATH];
    bool isEdge = false;

    if (GetModuleFileNameExW(hProcess, NULL, buffer, MAX_PATH)) {
        wstring fullPath(buffer);
        // 소문자로 변환
        transform(fullPath.begin(), fullPath.end(), fullPath.begin(), ::tolower);

        // 실행 파일명이 "msedge.exe" 인지 확인
        if (fullPath.find(L"msedge.exe") != wstring::npos) {
            isEdge = true;
        }
    }
    CloseHandle(hProcess);
    return isEdge;
}

// Edge 브라우저가 Foreground가 될 때까지 기다렸다가 IME를 변경하는 함수
void WaitAndSetIME() {
    LogDebug(L"Waiting for any 'msedge.exe' window to become foreground...");

    // 최대 5초 대기
    for (int i = 0; i < 50; i++) {
        HWND hForeground = GetForegroundWindow();

        if (hForeground != NULL) {
            DWORD fgPid = 0;
            GetWindowThreadProcessId(hForeground, &fgPid);

            if (IsEdgeProcess(fgPid)) {
                LogDebug(L"Detected Edge window! Attempting IME switch...");

                DWORD targetThreadId = GetWindowThreadProcessId(hForeground, NULL);
                DWORD currentThreadId = GetCurrentThreadId();

                // 1. AttachThreadInput 시도
                if (AttachThreadInput(currentThreadId, targetThreadId, TRUE)) {
                    HWND hFocus = GetFocus();
                    if (hFocus == NULL) hFocus = hForeground;

                    // 2. IME 상태 변경 시도
                    if (TrySetKoreanMode(hFocus)) {
                        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                        return; // 성공 시 종료
                    }
                    AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                }

                // 3. Fallback (IMM 실패 시)
                LogDebug(L"IMM failed. Executing Fallback (Key Press).");
                keybd_event(VK_HANGUL, 0, 0, 0);
                keybd_event(VK_HANGUL, 0, KEYEVENTF_KEYUP, 0);
                return; // Fallback 수행 후 종료
            }
        }

        // Edge가 아니면 0.1초 대기
        Sleep(100);
    }

    LogDebug(L"Timeout: No Edge window became foreground.");
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {

    // Custom Scheme으로 호출된 경우
    if (__argc > 1) {
        wstring arg = __wargv[1];
        // "einzime:" 프로토콜로 호출된 경우에만
        if (arg.find(L"einzime:") != wstring::npos) {
            WaitAndSetIME();
            return 0; // 작업 끝내고 종료
        }
    }

    // Native Messaging으로 호출했을 경우 (Extension 연결)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    while (true) {
        unsigned int length = 0;

        cin.read(reinterpret_cast<char*>(&length), 4);

        // 브라우저가 연결을 끊거나 파이프가 깨지면 루프 종료
        if (cin.eof()) break;
        if (length == 0) continue; // 0바이트가 올 경우 무시

        if (length > 0) {
            string message(length, ' ');
            cin.read(&message[0], length);

            // 로직 수행
            WaitAndSetIME();

            // 응답 전송
            string response = "{\"status\":\"ok\"}";
            unsigned int len = response.length();
            cout.write(reinterpret_cast<char*>(&len), 4);
            cout << response;
            cout.flush();
        }
    }
    return 0;
}