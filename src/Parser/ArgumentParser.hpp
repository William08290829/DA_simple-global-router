#pragma once    
#include <iostream>
#include <string>
#include <vector>


// 用來解析 <net file> <cell file> <output file>
class ArgumentParser {
public:
    std::string inputFile, outputFile, congestionMapFile;

    bool argparse(int argc, char* argv[]) {
        std::vector<std::string> positionalArgs;

        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];

            if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return false;
            }

            if (arg == "-c") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: missing output file after -c\n";
                    printUsage(argv[0]);
                    return false;
                }

                congestionMapFile = argv[++i];
                continue;
            }

            positionalArgs.push_back(arg);
        }

        if (positionalArgs.size() != 2) {
            printUsage(argv[0]);
            return false;
        }

        inputFile = positionalArgs[0];
        outputFile = positionalArgs[1];
        return true;
    }

    // 印出用法
    static void printUsage(const char* prog_name) {
        std::cerr << "Usage: " << prog_name << " <input txt file> <output txt file> [-c <congestion_map.txt>]\n"
                  << "Example:\n"
                  << "    " << prog_name << " ../testcase/proj_3_global_routing_inputs_v2.txt ../output/proj_3_global_routing_outputs_v2.txt\n"
                  << "    " << prog_name << " ../testcase/proj_3_global_routing_inputs_v2.txt ../output/proj_3_global_routing_outputs_v2.txt -c ../output/congestion_map.txt\n";
    }
};
