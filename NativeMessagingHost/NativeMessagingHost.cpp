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

// 현재 한글 상태인지 확인
bool IsKoreanModeByMessage(HWND targetWindow) {
    HWND hIME = ImmGetDefaultIMEWnd(targetWindow);

    if (hIME == NULL) {
        LogDebug(L"Failed to get Default IME Window.");
        return false;
    }

    // 메시지를 보내서 상태 확인 (IMC_GETOPENSTATUS)
    // if 0 = 영문(닫힘), else = 한글(열림)
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

// IME 강제 설정 변경 시도 함수
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

// PID Edge인지 판별하는 함수
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

// Edge 브라우저가 Foreground가 될 때까지 대기 후 IME를 변경하는 함수
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
                bool immSuccess = false;

                // AttachThreadInput 시도
                if (AttachThreadInput(currentThreadId, targetThreadId, TRUE)) {
                    LogDebug(L"AttachThreadInput Succeed! Attempting IME switch...");

                    HWND hFocus = GetFocus();
                    if (hFocus == NULL) hFocus = hForeground;

                    // ImmGetContext로 변경 시도
                    if (TrySetKoreanMode(hFocus)) {
                        immSuccess = true;
                    }
                    else if (ForceKoreanMode(hFocus)) {
                        immSuccess = true;
                    }
                    else {
                        LogDebug(L"All IMM APIs failed! (Sandbox blocked context)");
                    }

                    // ImmGetDefaultIMEWnd로 현재 IME 상태 확인
                    if (!immSuccess) {
                        if (IsKoreanModeByMessage(hFocus)) {
                            LogDebug(L"Check Result: Already Korean. Skipping Fallback.");
                            immSuccess = true; // 이미 한글이면 스킵
                        }
                    }

                    // 스레드 분리
                    AttachThreadInput(currentThreadId, targetThreadId, FALSE);

                    if (immSuccess) return;
                }
                else {
                    // Attach 실패 시
                    DWORD err = GetLastError();
                    wchar_t buf[100];
                    swprintf_s(buf, L"Attach Failed! Error Code: %d", err);
                    LogDebug(buf);
                }

                // 키보드 신호 발송으로 전환
                LogDebug(L"Executing Fallback: Excecute Key Press event.");
                keybd_event(VK_HANGUL, 0, 0, 0);
                keybd_event(VK_HANGUL, 0, KEYEVENTF_KEYUP, 0);

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

        // stdin에서 길이 읽기
        cin.read(reinterpret_cast<char*>(&length), 4);

        // 브라우저가 연결을 끊거나 파이프가 깨지면 루프 종료
        if (cin.eof()) break;
        if (length == 0) continue;

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