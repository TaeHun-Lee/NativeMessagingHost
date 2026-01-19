window.addEventListener("message", (event) => {
    if (event.source !== window) return;

    if (event.data.type && event.data.type === "REQ_IME_KOREAN") {
        console.log("[Extension] 한글 전환 요청 수신. Background로 전달합니다.");
        
        // Background Service Worker로 메시지 전달
        chrome.runtime.sendMessage({ action: "SWITCH_TO_KOREAN" });
    }
});