#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <imm.h>
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

#ifndef IMC_SETCONVERSIONMODE
#define IMC_SETCONVERSIONMODE 0x0002
#endif

// [수동 정의] imm.h 포함이 안 될 경우를 위한 안전장치
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif

#ifndef IMC_SETOPENSTATUS
#define IMC_SETOPENSTATUS 0x0006
#endif

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

// 메시지 전송 방식으로 현재 한글 상태인지 확인
bool IsKoreanModeByMessage(HWND targetWindow) {
    HWND hIME = ImmGetDefaultIMEWnd(targetWindow);

    if (hIME == NULL) {
        LogDebug(L"Failed to get Default IME Window.");
        return false;
    }

    // 메시지를 보내서 상태 확인 (IMC_GETOPENSTATUS)
    // if 0 = 영문(닫힘), else 한글(열림)
    LRESULT status = SendMessage(hIME, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0);
    if (status != 0) {
        LogDebug(L"Check Result: KOREAN (Open)");
        return true;
    }
    else {
        LogDebug(L"Check Result: ENGLISH (Closed) or Failed");
        return false;
    }
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

// IME '한글 모드'로 강제 설정 변경 시도 함수
bool ForceKoreanMode(HWND targetWindow) {
    HIMC hImc = ImmGetContext(targetWindow);
    if (hImc == NULL) return false;

    bool result = false;
    DWORD dwConversion, dwSentence;
    if (ImmGetConversionStatus(hImc, &dwConversion, &dwSentence)) {
        DWORD newConversion = dwConversion | IME_CMODE_HANGUL | IME_CMODE_NATIVE;
        if (ImmSetConversionStatus(hImc, newConversion, dwSentence)) {
            LogDebug(L"Success: Forced IME to Korean Mode.");
            result = true;
        }
    }
    else {
        if (ImmSetConversionStatus(hImc, IME_CMODE_HANGUL | IME_CMODE_NATIVE, 0)) {
            LogDebug(L"Success: Blindly Forced IME to Korean Mode.");
            result = true;
        }
    }

    // 윈도우 메시지로 강제 시도
    SendMessage(targetWindow, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, IME_CMODE_HANGUL | IME_CMODE_NATIVE);

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

                // AttachThreadInput 시도
                if (AttachThreadInput(currentThreadId, targetThreadId, TRUE)) {
                    LogDebug(L"AttachThreadInput Succeed! Attempting IME switch...");
                    HWND hFocus = GetFocus();
                    if (hFocus == NULL) hFocus = hForeground;
                    bool immSuccess = false;

                    // IME 상태 변경 시도
                    if (TrySetKoreanMode(hFocus)) {
                        LogDebug(L"TrySetKoreanMode Succeed!");
                        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                        immSuccess = true;
                    }
					// 강제 변경 시도
                    else if (ForceKoreanMode(hFocus)) {
                        LogDebug(L"ForceKoreanMode Succeed!");
                        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                        immSuccess = true;
                    }
                    // ImmGetContext가 NULL을 뱉었을 경우
                    else {
                        LogDebug(L"All IMM APIs failed! (Sandbox blocked context)");
                    }
                    // 스레드 분리
                    AttachThreadInput(currentThreadId, targetThreadId, FALSE);
                    if (immSuccess) return;
                }
                else {
                    // 에러 코드 5 (ACCESS_DENIED): 권한 문제 또는 32/64비트 불일치
                    // 에러 코드 87 (INVALID_PARAMETER): TID 문제
                    LogDebug(L"AttachThreadInput failed!");
                    DWORD err = GetLastError();
                    wchar_t buf[100];
                    swprintf_s(buf, L"Attach Failed! Error Code: %d", err);
                    LogDebug(buf);
                }

                // AttachThreadInput이 실패하거나 ImmGetContext가 실패했을 경우 시도
                if (IsKoreanModeByMessage(hForeground)) {
                    LogDebug(L"Already Korean Mode. No action needed.");
                }
                else {
                    LogDebug(L"Not in Korean Mode. Executing Fallback (Key Press)");
                    // 키보드 신호로 전환 (IMM 실패 시)
                    keybd_event(VK_HANGUL, 0, 0, 0);
                    keybd_event(VK_HANGUL, 0, KEYEVENTF_KEYUP, 0);
                }
                return;
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