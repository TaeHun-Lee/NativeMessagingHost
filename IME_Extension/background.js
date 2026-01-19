const HOST_NAME = "com.einz.imehelper";

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message.action === "SWITCH_TO_KOREAN") {
        console.log("[Background] Native App 연결 시도...");

        // Native App 연결 및 메시지 전송
        chrome.runtime.sendNativeMessage(HOST_NAME, { text: "switch_korean" }, (response) => {
            if (chrome.runtime.lastError) {
                console.error("[Background] [Error] Native App 연결 실패:", chrome.runtime.lastError.message);
                return;
            }
            console.log("[Background] Native App 응답:", response);
        });
    }
});