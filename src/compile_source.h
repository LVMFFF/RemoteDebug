#include <json.hpp> // for nlohmann::json
#include <iostream>
#include <fstream>
#include <string>
#include <optional>
#include <unordered_map>

namespace LVMF {

class CompileCommandManager {
public:
    CompileCommandManager(const std::string &compile_file) : m_compile_file(compile_file)
    {
        std::ifstream file(compile_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open compile command file: " << compile_file << std::endl;
            return;
        }

        using json = nlohmann::json;
        json data = json::parse(file);
        for (const auto &entry : data) {
            if (entry.contains("file") && entry.contains("command")) {
                std::string source_file = entry["file"].get<std::string>();
                std::string command = entry["command"].get<std::string>();

                // 删除 command 参数中的 -o 参数，后续由脚本指定
                size_t pos_o = command.find("-o ");
                if (pos_o != std::string::npos) {
                    size_t end_pos = command.find(' ', pos_o + 3); // 跳过 "-o "
                    if (end_pos != std::string::npos) {
                        command = command.substr(0, pos_o) + command.substr(end_pos);
                    } else {
                        command = command.substr(0, pos_o);
                    }
                }

                m_fullpath_2_command[source_file] = command;

                std::string basename = source_file.substr(source_file.find_last_of("/\\") + 1);
                m_basename_2_fullname[basename] = source_file;
            }
        }
    }

    /**
     * @brief 编译源文件
     * @param source_file 源文件路径
     * @param output_file 输出文件路径
     * @return 返回0表示成功，其他值表示失败
     */
    int compile_source_file(const std::string &source_file, const std::string &output_file)
    {
        const auto &command = get_compile_command(source_file);
        if (!command) {
            std::cerr << "No compile command found for source file: " << source_file << std::endl;
            return -1; // 未找到编译命令
        }

        std::string full_command = *command + " -o " + output_file;

    }

    /**
     * @brief 打印所有源文件的编译命令
     */
    void dump()
    {
        std::cout << "----------- fullpath -> command: -----------\n";
        for (const auto &[source_file, command] : m_fullpath_2_command) {
            std::cout << "{\n    Source file: " << source_file << "\n";
            std::cout << "    Compile command: " << command <<  "\n}\n";
        }

        std::cout << "\n----------- basename -> fullpath: -----------\n";
        for (const auto &[basename, full_path] : m_basename_2_fullname) {
            std::cout << "{\n    Basename: " << basename << "\n";
            std::cout << "    Full path: " << full_path << "\n}\n";
        }
    }

    /**
     * @brief 获取源文件的编译参数
     * @param source_file 源文件名称
     * @return 返回编译命令，如果未找到则返回std::nullopt
     */
    std::optional<std::string> get_compile_command(const std::string &source_file)
    {
        if (m_fullpath_2_command.count(source_file)) {
            return m_fullpath_2_command[source_file];
        }

        if (m_basename_2_fullname.count(source_file)) {
            const auto &fullname = m_basename_2_fullname[source_file];
            if (m_fullpath_2_command.count(fullname)) {
                return m_fullpath_2_command[fullname];
            }
        }
        return std::nullopt;
    }

private:
    std::string m_compile_file;
    std::unordered_map<std::string, std::string> m_fullpath_2_command; // 完整路径->编译命令
    std::unordered_map<std::string, std::string> m_basename_2_fullname; // 文件基名->完整路径名
};

};