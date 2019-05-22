﻿//
// Created by User on 2019/3/29.
//

#include "serial_port.hh"

#ifdef _MSC_VER

#include <vector>
#include <thread>
#include "macros.h"

#include <Windows.h>

#define TRY(OPERATION) if(!OPERATION) THROW(#OPERATION, GetLastError())

serial_port::serial_port(const std::string &name,
                         unsigned int baud_rate,
                         size_t in_buffer_size,
                         size_t out_buffer_size) {
    
    auto temp = std::string(R"(\\.\)") + name;
    handle = CreateFileA(temp.c_str(),  // 串口名，`COM9` 之后需要前缀
                         GENERIC_READ | GENERIC_WRITE, // 读和写
                         0,                            // 独占模式
                         nullptr,                      // 子进程无权限
                         OPEN_EXISTING,                // 打开设备
                         FILE_FLAG_OVERLAPPED,
                         nullptr);
    
    if (handle == INVALID_HANDLE_VALUE)
        THROW("CreateFileA(...)", GetLastError());
    
    // 设置端口设定
    DCB dcb;
    TRY(GetCommState(handle, &dcb));
    dcb.BaudRate = baud_rate;
    dcb.ByteSize = 8;
    TRY(SetCommState(handle, &dcb));
    
    // 设置超时时间
    COMMTIMEOUTS timeouts{3, 1, 0, 10, 0};
    TRY(SetCommTimeouts(handle, &timeouts));
    
    // 设置缓冲区容量
    TRY(SetupComm(handle, in_buffer_size, out_buffer_size));
    
    // 订阅事件
    TRY(SetCommMask(handle, EV_RXCHAR));
}

serial_port::~serial_port() {
    auto temp = handle.exchange(nullptr);
    if (!temp) return;
    break_read();
    CloseHandle(temp);
}

void WINAPI callback(DWORD error_code,
                     DWORD actual,
                     LPOVERLAPPED overlapped) {
    delete static_cast<std::vector<uint8_t> *>(overlapped->hEvent);
    delete overlapped;
    
    if (error_code != ERROR_SUCCESS)
        THROW("WriteFileEx", error_code);
}

void serial_port::send(const uint8_t *buffer, size_t size) {
    if (size <= 0) return;
    
    auto overlapped = new OVERLAPPED{};
    auto ptr        = new std::vector<uint8_t>(buffer, buffer + size);
    overlapped->hEvent = ptr;
    WriteFileEx(handle, ptr->data(), size, overlapped, &callback);
    SleepEx(INFINITE, true);
}

size_t serial_port::read(uint8_t *buffer, size_t size) {
    weak_lock_guard lock(read_mutex);
    if (!lock) return 0;
    
    DWORD      event = 0;
    OVERLAPPED overlapped{};
    
    do {
        overlapped.hEvent = CreateEventA(nullptr, true, false, nullptr);
        if (!WaitCommEvent(handle, &event, &overlapped)) {
            auto condition = GetLastError();
            if (condition != ERROR_IO_PENDING)
                THROW("WaitCommEvent", condition);
        }
        
        DWORD progress = 0;
        GetOverlappedResult(handle, &overlapped, &progress, true);
        if (event == 0) return 0;
    } while (event != EV_RXCHAR);
    
    ReadFile(handle, buffer, size, nullptr, &overlapped);
    auto condition = GetLastError();
    switch (condition) {
        case ERROR_SUCCESS:
        case ERROR_IO_PENDING: {
            DWORD actual = 0;
            GetOverlappedResult(handle, &overlapped, &actual, true);
            return actual;
        }
        default:
            THROW("ReadFile", condition);
    }
}

void serial_port::break_read() const {
    weak_lock_guard lock(read_mutex);
    
    while (!lock.retry()) {
        SetCommMask(handle, EV_RXCHAR);
        std::this_thread::yield();
    }
}

#endif
