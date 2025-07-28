#include "compile_source.h"

int main() {
    // Initialize the CompileCommandManager with a sample compile command file
    CompileCommandManager manager("/mnt/d/code/RemoteDebug/build/compile_commands.json");

    // Get compile command for a specific source file
    auto command = manager.get_compile_command("/mnt/d/code/RemoteDebug/test/compile_source_test.cpp");
    if (command) {
        std::cout << "Compile command for example.cpp: " << *command << std::endl;
    } else {
        std::cerr << "Failed to retrieve compile command for example.cpp." << std::endl;
    }

    // Dump all compile commands
    // manager.dump();

    return 0;
}