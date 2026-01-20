// 클라이언트에서 온 요청을 Background로 전달
window.addEventListener("message", (event) => {
    if (event.source !== window) return;

    if (event.data.type && event.data.type === "REQ_IME_KOREAN") {
        console.log("[Extension] 한글 전환 요청 수신. Background로 전달합니다.");
        chrome.runtime.sendMessage({ type: "REQ_IME_KOREAN" });
    }
});

// Background에서 온 응답을 클라이언트로 전달
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message.type === "RES_IME_KOREAN") {
        console.log("[Extension] Background 응답 수신. 클라이언트로 전달합니다:", message.payload);
        window.postMessage({
            type: "RES_IME_KOREAN",
            payload: message.payload
        }, "*");
    } else if (message.type === "ERR_IME_KOREAN") {
        console.error("[Extension] Background 에러 응답 수신. 클라이언트로 전달합니다:", message.payload);
        window.postMessage({
            type: "ERR_IME_KOREAN",
            payload: message.payload
        }, "*");
    }
});