#include <libssh2.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdexcept>

// Utility to handle errors
void checkLibssh2Result(int result, const std::string& errorMessage) {
    if (result != 0) {
        throw std::runtime_error(errorMessage + " (Error code: " + std::to_string(result) + ")");
    }
}

void sendFileAndExecuteCommand(const std::string& hostname,
                               int port,
                               const std::string& username,
                               const std::string& password,
                               const std::string& localFilePath,
                               const std::string& remoteFilePath,
                               const std::string& command) {
    // Initialize libssh2
    libssh2_init(0);

    // Establish a socket connection
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    inet_pton(AF_INET, hostname.c_str(), &sin.sin_addr);

    if (connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(sock);
        throw std::runtime_error("Failed to connect to host");
    }

    // Establish an SSH session
    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        close(sock);
        throw std::runtime_error("Failed to initialize SSH session");
    }

    checkLibssh2Result(libssh2_session_handshake(session, sock), "SSH handshake failed");

    // Authenticate with username and password
    checkLibssh2Result(libssh2_userauth_password(session, username.c_str(), password.c_str()),
                       "Authentication failed");

    // SCP to send the file
    struct stat fileinfo;
    if (stat(localFilePath.c_str(), &fileinfo) != 0) {
        throw std::runtime_error("Failed to get local file info");
    }

    FILE* localFile = fopen(localFilePath.c_str(), "rb");
    if (!localFile) {
        throw std::runtime_error("Failed to open local file");
    }

    LIBSSH2_CHANNEL* channel = libssh2_scp_send(session, remoteFilePath.c_str(), fileinfo.st_mode & 0777, fileinfo.st_size);
    if (!channel) {
        fclose(localFile);
        throw std::runtime_error("SCP send failed");
    }

    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), localFile)) > 0) {
        libssh2_channel_write(channel, buffer, n);
    }

    fclose(localFile);
    libssh2_channel_send_eof(channel);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);

    // Execute the command
    channel = libssh2_channel_open_session(session);
    if (!channel) {
        throw std::runtime_error("Failed to open channel for command execution");
    }

    checkLibssh2Result(libssh2_channel_exec(channel, command.c_str()), "Command execution failed");

    // Read the command output
    while ((n = libssh2_channel_read(channel, buffer, sizeof(buffer))) > 0) {
        std::cout.write(buffer, n);
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);

    // Cleanup
    libssh2_session_disconnect(session, "Normal Shutdown");
    libssh2_session_free(session);
    close(sock);
    libssh2_exit();
}

int main() {
    try {
        sendFileAndExecuteCommand("192.168.1.10", 22, "username", "password",
                                  "/path/to/local/file.txt",
                                  "/path/to/remote/file.txt",
                                  "ls -l /path/to/remote/");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}