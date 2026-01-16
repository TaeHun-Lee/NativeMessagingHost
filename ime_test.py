import struct
import subprocess
import sys
import time

# 테스트할 EXE 경로 (빌드된 경로로 수정하세요)
EXE_PATH = "x64/Release/NativeMessagingHost.exe"

def send_message(proc, message):
    # Native Messaging 프로토콜: 길이(4바이트) + JSON문자열
    encoded_msg = message.encode('utf-8')
    header = struct.pack('I', len(encoded_msg)) # 'I'는 unsigned int (4bytes)
    
    print(f"Sending: {message}")
    proc.stdin.write(header)
    proc.stdin.write(encoded_msg)
    proc.stdin.flush()

def read_message(proc):
    # 응답 읽기: 길이(4바이트) -> 본문
    raw_length = proc.stdout.read(4)
    if not raw_length:
        return None
    
    length = struct.unpack('I', raw_length)[0]
    content = proc.stdout.read(length).decode('utf-8')
    return content

try:
    # 프로그램을 실행 (stdin/stdout 파이프 연결)
    proc = subprocess.Popen(
        EXE_PATH, 
        stdin=subprocess.PIPE, 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE
    )

    # 1. 메시지 전송 (로그인 완료 신호 흉내)
    send_message(proc, '{"text": "login_success"}')

    # 2. 프로그램이 Edge 프로세스를 찾을 시간(약 10초) 동안 대기하거나
    #    테스트를 위해 Edge를 켜놓고 포커스를 왔다갔다 해보세요.
    print("Waiting for response (Check DebugView as well)...")
    
    # 응답 대기 (Blocking)
    response = read_message(proc)
    print(f"Received: {response}")

    proc.terminate()

except Exception as e:
    print(f"Error: {e}")