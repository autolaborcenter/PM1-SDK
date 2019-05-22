﻿//
// Created by User on 2019/5/16.
//

#ifndef SERIAL_PORT_ASYNC_MACROS_H
#define SERIAL_PORT_ASYNC_MACROS_H


#include <sstream>
#include <mutex>

/** 受控锁 */
class weak_lock_guard {
    volatile bool locked;
    std::mutex    &lock;

public:
    explicit weak_lock_guard(std::mutex &lock)
        : lock(lock),
          locked(lock.try_lock()) {}
    
    explicit operator bool() const { return locked; }
    
    bool retry() {
        return locked ? true : locked = lock.try_lock();
    }
    
    ~weak_lock_guard() { if (locked) lock.unlock(); }
};

inline std::string error_info_string(const std::string &prefix,
                                     long long code,
                                     const char *file = nullptr,
                                     int line = 0) noexcept {
    std::stringstream builder;
    builder << "error occurred in class serial_port, when "
            << prefix << " with code " << code;
    if (file)
        builder << std::endl << file << '(' << line << ')';
    return builder.str();
}

inline std::string error_info_string(const std::string &prefix,
                                     const std::string &msg,
                                     const char *file = nullptr,
                                     int line = 0) noexcept {
    std::stringstream builder;
    builder << "error occurred in class serial_port, when "
            << prefix << " with msg \"" << msg << "\"";
    if (file)
        builder << std::endl << file << '(' << line << ')';
    return builder.str();
}

#ifdef _DEBUG
#define THROW(INFO, CODE) throw std::logic_error(error_info_string(INFO, CODE, __FILE__, __LINE__).c_str())
#else
#define THROW(INFO, MSG)  throw std::logic_error(error_info_string(INFO, MSG).c_str())
#endif // DEBUG


#endif // SERIAL_PORT_ASYNC_MACROS_H
