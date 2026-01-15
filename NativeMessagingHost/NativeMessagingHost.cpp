#include <iostream>
#include <vector>
#include <windows.h>
#include <tlhelp32.h> // 프로세스 스냅샷용
#include <imm.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "user32.lib")

using namespace std;

// 로깅 헬퍼
void LogDebug(const wstring& msg) {
    OutputDebugStringW((L"[IME_HELPER] " + msg + L"\n").c_str());
}

// 1. 내 프로세스의 부모 프로세스 ID(즉, Edge)를 찾는 함수
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

// 2. 특정 PID를 가진 프로세스의 메인 윈도우 핸들을 찾는 콜백 데이터
struct WindowSearchData {
    DWORD targetPid;
    HWND resultHwnd;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    WindowSearchData* data = reinterpret_cast<WindowSearchData*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);

    // 해당 프로세스의 윈도우이면서, 눈에 보이는(Visible) 창만 찾음
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

// 3. IME 변경 시도 함수 (이전과 동일)
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

// 4. [핵심] Edge가 활성화될 때까지 기다렸다가 처리하는 함수
void WaitAndSetIME() {
    // 부모(Edge) 프로세스 ID 찾기
    DWORD parentPid = GetParentProcessId();
    if (parentPid == 0) {
        LogDebug(L"Error: Could not find parent process.");
        return;
    }

    LogDebug(L"Waiting for Edge (Parent PID) to become foreground...");

    // 최대 10초 대기 (0.1초 간격으로 100번 체크)
    for (int i = 0; i < 100; i++) {
        HWND hForeground = GetForegroundWindow();
        if (hForeground != NULL) {
            DWORD fgPid = 0;
            GetWindowThreadProcessId(hForeground, &fgPid);

            // 현재 활성화된 창이 나의 부모(Edge)인가?
            if (fgPid == parentPid) {

                // 스레드 연결 및 IME 변경 시도
                DWORD targetThreadId = GetWindowThreadProcessId(hForeground, NULL);
                DWORD currentThreadId = GetCurrentThreadId();

                if (AttachThreadInput(currentThreadId, targetThreadId, TRUE)) {
                    HWND hFocus = GetFocus(); // 실제 입력창 핸들
                    if (hFocus == NULL) hFocus = hForeground;

                    if (TrySetKoreanMode(hFocus)) {
                        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                        return; // 성공했으니 즉시 종료
                    }
                    AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                }

                // IME 변경 실패 시 Fallback
                keybd_event(VK_HANGUL, 0, 0, 0);
                keybd_event(VK_HANGUL, 0, KEYEVENTF_KEYUP, 0);
                LogDebug(L"Fallback Executed.");
                return; // Fallback 실행 후 종료
            }
        }

        // Edge가 아직 활성화되지 않았으면 0.1초 대기
        Sleep(100);
    }

    LogDebug(L"Timeout: Edge did not become foreground in time.");
}

int main() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    while (true) {
        unsigned int length = 0;
        cin.read(reinterpret_cast<char*>(&length), 4);
        if (cin.eof()) break;

        if (length > 0) {
            string message(length, ' ');
            cin.read(&message[0], length);

            // 메시지를 받으면 "기다렸다가 실행" 로직 시작
            WaitAndSetIME();

            string response = "{\"status\":\"ok\"}";
            unsigned int len = response.length();
            cout.write(reinterpret_cast<char*>(&len), 4);
            cout << response;
            cout.flush();
        }
    }
    return 0;
}