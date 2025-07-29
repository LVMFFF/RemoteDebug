#include "compile_source.h"
#include <vector>
#include <filesystem>

using namespace LVMF;

int main() {

    CompileCommandManager manager("/mnt/d/code/RemoteDebug/build/compile_commands.json");

    auto fullname = "/mnt/d/code/RemoteDebug/test/compile_source_test.cpp";
    auto command = manager.get_compile_command(fullname);
    if (command) {
        std::cout << "Compile command for " << fullname << ": " <<  *command << std::endl;
    } else {
        std::cerr << "Failed to retrieve compile command for compile_source_test.cpp." << std::endl;
    }

    auto basename = "compile_source_test.cpp";
    auto basename2command = manager.get_compile_command(basename);
    if (basename2command) {
        std::cout << "Compile command for " << basename << ": " <<  *basename2command << std::endl;
    } else {
        std::cerr << "Failed to retrieve compile command for " << basename << std::endl;
    }

    auto not_found = manager.get_compile_command("non_existent_file.cpp");
    if (not_found) {
        std::cout << "Compile command for non_existent_file.cpp: \n";
    }
    // Dump all compile commands
    manager.dump();

    // 编译源文件
    std::vector<std::string> options = {
    };
    [[maybe_unused]] auto compile_current_file = manager.compile_source_file(__FILE__, options, std::filesystem::current_path());
    return 0;
}