const HOST_NAME = "com.einz.imehelper";

// Content Script(웹페이지)로부터 메시지 수신
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    
    // 한글 전환 요청인 경우
    if (request.type === "REQ_IME_KOREAN") {
        console.log(`[Background] 요청 수신 (Tab ID: ${sender.tab.id})`);

        // Native App에 연결
        const port = chrome.runtime.connectNative(HOST_NAME);

        // Native App으로 메시지 발송
        port.postMessage({ text: "trigger" });

        // Native App으로부터 응답 수신
        port.onMessage.addListener((nativeResponse) => {
            console.log("[Background] Native App 응답 수신:", nativeResponse);

            // 요청을 보냈던 탭(Content Script)으로 결과 회신
            if (sender.tab && sender.tab.id) {
                chrome.tabs.sendMessage(sender.tab.id, {
                    type: "RES_IME_KOREAN",  // 응답용 타입
                    payload: nativeResponse  // {status: 'ok'}
                });
            }
            // 연결 종료
            port.disconnect();
        });

        // 연결 에러 처리
        port.onDisconnect.addListener(() => {
            if (chrome.runtime.lastError) {
                console.error("[Background] Native App 연결 에러:", chrome.runtime.lastError.message);
                if (sender.tab && sender.tab.id) {
                    chrome.tabs.sendMessage(sender.tab.id, {
                        type: "ERR_IME_KOREAN",            // 에러 응답용 타입
                        payload: chrome.runtime.lastError  // 에러 정보
                    });
                }
            }
        });
    }
});