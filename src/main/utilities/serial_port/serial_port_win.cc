﻿//
// Created by User on 2019/3/29.
//

#include "serial_port.hh"

#ifdef _MSC_VER

#include <vector>
#include <thread>

#include <Windows.h>

#include "macros.h"
#include <utilities/raii/weak_lock_guard.hpp>

#define TRY(OPERATION) if(!OPERATION) THROW(#OPERATION, GetLastError())

serial_port::serial_port(const std::string &name,
                         unsigned int baud_rate,
                         uint8_t check_period,
                         uint8_t wait_period,
                         size_t in_buffer_size,
                         size_t out_buffer_size) {
    
    auto temp = std::string(R"(\\.\)") + name;
    handle = CreateFileA(temp.c_str(),                                // 串口名，`COM9` 之后需要前缀
                         GENERIC_READ | GENERIC_WRITE, // 读和写
                         0,                              // 独占模式
                         nullptr,                    // 子进程无权限
                         OPEN_EXISTING,                               // 打开设备
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
    COMMTIMEOUTS timeouts{check_period, wait_period, 0, 10, 0};
    TRY(SetCommTimeouts(handle, &timeouts));
    
    // 设置缓冲区容量
    TRY(SetupComm(handle, in_buffer_size, out_buffer_size));
    
    // 订阅事件
    TRY(SetCommMask(handle, EV_RXCHAR));
}

serial_port::~serial_port() noexcept {
    auto temp = handle.exchange(nullptr);
    if (!temp) return;
    PurgeComm(temp, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    std::lock_guard<decltype(read_mutex)> lock(read_mutex);
    CloseHandle(temp);
}

struct tool_t {
    std::vector<uint8_t> buffer_ptr;
    void                 *handle;
};

void WINAPI callback(
    DWORD error_code,
    DWORD actual,
    LPOVERLAPPED overlapped
) {
    auto tool = static_cast<tool_t *>(overlapped->hEvent);
    
    if (error_code != ERROR_SUCCESS)
        PurgeComm(tool->handle, PURGE_TXABORT | PURGE_TXCLEAR);
    delete tool;
    delete overlapped;
}

void serial_port::send(const uint8_t *buffer, size_t size) noexcept {
    if (size <= 0 || !handle.load()) return;
    
    auto overlapped = new OVERLAPPED{};
    auto ptr        = new tool_t{
        std::vector<uint8_t>(buffer, buffer + size),
        handle.load()
    };
    overlapped->hEvent = ptr;
    WriteFileEx(handle, ptr->buffer_ptr.data(), size, overlapped, &callback);
    SleepEx(INFINITE, true);
}

size_t serial_port::read(uint8_t *buffer, size_t size) {
    if (!handle.load()) return 0;
    weak_lock_guard<std::mutex> lock(read_mutex);
    if (!lock) return 0;
    
    DWORD      event = 0, error;
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventA(nullptr, true, false, nullptr);
    
    do {
        ResetEvent(overlapped.hEvent);
        if (!WaitCommEvent(handle, &event, &overlapped)) {
            if ((error = GetLastError()) != ERROR_IO_PENDING)
                THROW("WaitCommEvent", error);
            DWORD progress = 0;
            GetOverlappedResult(handle, &overlapped, &progress, true);
        }
        if (event == 0) return 0;
    } while (!(event & EV_RXCHAR));
    
    ReadFile(handle, buffer, size, nullptr, &overlapped);
    switch (error = GetLastError()) {
        case ERROR_SUCCESS:
        case ERROR_IO_PENDING: {
            DWORD actual = 0;
            GetOverlappedResult(handle, &overlapped, &actual, true);
            return actual;
        }
        default:
            PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR);
            THROW("ReadFile", error);
    }
}

void serial_port::break_read() const noexcept {
    if (!handle.load()) return;
    weak_lock_guard lock(read_mutex);
    
    while (!lock.retry()) {
        SetCommMask(handle, EV_RXCHAR);
        std::this_thread::yield();
    }
}

#endif
