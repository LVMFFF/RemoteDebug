// Copyright (c) 2025 The RemoteDebug Authors. All rights reserved.
// 传输文件基类：发送热补丁工具、本地生成的热补丁文件到远端设备

#pragma once

#include <string>

namespace RemoteDebug {

enum class TRANSMIT_TYPE {
    FTP,
    TELNET,
    SSH,
    NONE
};

struct Device_info {
    TRANSMIT_TYPE     trans_type;         // 设备传输类型
    uint32_t          ip{ UINT32_MAX };   // 设备IP地址
    uint32_t          port{ UINT32_MAX }; // 端口号
    const std::string file_name;
    const std::string m_username; // FTP用户名
    const std::string m_password; // FTP密码
};

class RemoteTransmit {
    public:
    int transmit(Device_info) = 0; // 传输文件到远端
};

}; // namespace RemoteDebug
