// Copyright (c) 2025 The RemoteDebug Authors. All rights reserved.
// 使用 ftp 连接设备进行传输

#pragma once

#include "transmit.h"

class FtpTransmit : public RemoteTransmit {
public:
    FtpTransmit(const Device_info &device_info) : m_device_info(device_info) {}
    int transmit() override;

private:
    int connect();
    int send_file();

private:
    int m_socket_fd{ -1 };
};
