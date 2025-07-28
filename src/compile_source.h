#include <json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <optional>

class CompileCommandManager {
public:
    using json = nlohmann::json;

    CompileCommandManager(const std::string &compile_file) : m_compile_file(compile_file)
    {
        std::ifstream file(compile_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open compile command file: " << compile_file << std::endl;
            return;
        }
        m_command = json::parse(file);
    }

    /**
     * @brief 获取源文件的编译参数
     * @param source_file 源文件名称
     */
    std::optional<std::string> get_compile_command(const std::string &source_file)
    {
        if (m_command.contains(source_file) && m_command[source_file].is_string()) {
            return m_command[source_file].get<std::string>();
        }
        return std::nullopt;
    }

    /**
     * @brief 打印所有源文件的编译命令
     */
    void dump()
    {
        for (const auto &[source_file, command] : m_command.items()) {
            std::cout << "Source file: " << source_file << std::endl;
            std::cout << "Compile command: " << command << std::endl;
        }
    }

private:
    std::string m_compile_file;
    json m_command;
};

