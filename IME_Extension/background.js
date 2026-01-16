// Native Messaging Host의 고유 이름 (나중에 레지스트리에 등록할 이름과 같아야 함)
const HOST_NAME = "com.einz.imehelper"; 

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message.action === "SWITCH_TO_KOREAN") {
        console.log("[Background] Native App 연결 시도...");

        // 1. Native App 연결 및 메시지 전송
        chrome.runtime.sendNativeMessage(HOST_NAME, { text: "switch_korean" }, (response) => {
            
            // 2. 에러 처리 (EXE가 없거나 레지스트리 미등록 시)
            if (chrome.runtime.lastError) {
                console.error("[Error] Native App 연결 실패:", chrome.runtime.lastError.message);
                return;
            }

            console.log("[Background] Native App 응답:", response);
        });
    }
});