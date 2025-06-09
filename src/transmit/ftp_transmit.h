// Copyright (c) 2025 The RemoteDebug Authors. All rights reserved.
// 使用 ftp 连接设备进行传输

#pragma once

#include "transmit.h"

class FtpTransmit : public RemoteTransmit {
public:
    int transmit(Device_info);

private:
    int connect();
    int send_file(const Device_info& device_info);

private:
    int m_socket_fd{ -1 }; // 套接字文件描述符
    std::string m_file_path; // 要传输的文件路径
};
