﻿#if defined(_MSC_VER)

/*
 * Copyright (c) 2014 Craig Lilley <cralilley@gmail.com>
 * This software is made available under the terms of the MIT licence.
 * A copy of the licence can be obtained from:
 * http://opensource.org/licenses/MIT
 */

#pragma comment(lib, "SetupAPI.Lib")

#include "../serial.h"
#include <tchar.h>
#include <Windows.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <devguid.h>

constexpr DWORD port_name_max_length     = 256;
constexpr DWORD friendly_name_max_length = 256;
constexpr DWORD hardware_id_max_length   = 256;

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring &wstr) {
    int         size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}

std::vector<serial::PortInfo>
serial::list_ports() {
    std::vector<PortInfo> devices_found;
    
    HDEVINFO device_info_set = SetupDiGetClassDevs(
        (const GUID *) &GUID_DEVCLASS_PORTS,
        nullptr,
        nullptr,
        DIGCF_PRESENT);
    
    unsigned int    device_info_set_index = 0;
    SP_DEVINFO_DATA device_info_data;
    
    device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
    
    while (SetupDiEnumDeviceInfo(device_info_set, device_info_set_index, &device_info_data)) {
        device_info_set_index++;
        
        // Get port name
        
        HKEY hkey              = SetupDiOpenDevRegKey(
            device_info_set,
            &device_info_data,
            DICS_FLAG_GLOBAL,
            0,
            DIREG_DEV,
            KEY_READ);
        
        TCHAR port_name[port_name_max_length];
        DWORD port_name_length = port_name_max_length;
        
        LONG return_code = RegQueryValueEx(
            hkey,
            _T("PortName"),
            nullptr,
            nullptr,
            (LPBYTE) port_name,
            &port_name_length);
        
        RegCloseKey(hkey);
        
        if (return_code != EXIT_SUCCESS)
            continue;
        
        if (port_name_length > 0 && port_name_length <= port_name_max_length)
            port_name[port_name_length - 1] = '\0';
        else
            port_name[0]                  = '\0';
        
        // Ignore parallel ports
        
        if (_tcsstr(port_name, _T("LPT")) != nullptr)
            continue;
        
        // Get port friendly name
        
        TCHAR friendly_name[friendly_name_max_length];
        DWORD friendly_name_actual_length = 0;
        
        BOOL got_friendly_name = SetupDiGetDeviceRegistryProperty(
            device_info_set,
            &device_info_data,
            SPDRP_FRIENDLYNAME,
            nullptr,
            (PBYTE) friendly_name,
            friendly_name_max_length,
            &friendly_name_actual_length);
        
        if (got_friendly_name == TRUE && friendly_name_actual_length > 0)
            friendly_name[friendly_name_actual_length - 1] = '\0';
        else
            friendly_name[0]            = '\0';
        
        // Get hardware ID
        
        TCHAR hardware_id[hardware_id_max_length];
        DWORD hardware_id_actual_length = 0;
    
        auto got_hardware_id = SetupDiGetDeviceRegistryProperty(
            device_info_set,
            &device_info_data,
            SPDRP_HARDWAREID,
            nullptr,
            (PBYTE) hardware_id,
            hardware_id_max_length,
            &hardware_id_actual_length);
    
        if (got_hardware_id && hardware_id_actual_length > 0)
            hardware_id[hardware_id_actual_length - 1] = '\0';
        else
            hardware_id[0] = '\0';
        
        #ifdef UNICODE
        devices_found.push_back(
            PortInfo{utf8_encode(portName),
                     utf8_encode(friendlyName),
                     utf8_encode(hardwareId)
            }
        );
        #else
        devices_found.push_back(
            PortInfo{port_name,
                     friendly_name,
                     hardware_id
            }
        );
        #endif
    }
    
    SetupDiDestroyDeviceInfoList(device_info_set);
    
    return devices_found;
}

#endif // #if defined(_WIN32)
