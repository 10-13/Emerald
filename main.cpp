#include "hdrs/emerald.hpp"

int main(int argc, char* argv[]) {
    auto mappings = echolang::echo_mapping::create_default_controls();
    Emerald::EchoExtension::SetupEmeraldMappings(&mappings);
    std::cout << "Compile path:\n";
    std::string path{argv[1]};
    std::cout << path << std::endl;
    //std::getline(std::cin, path);

    auto script = std::make_shared<echolang::echo_script>();
    script->from_file(path);

    std::cout << *script;

    echolang::executor exec{script, mappings};

    exec.run();
}

