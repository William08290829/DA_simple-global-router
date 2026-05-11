#pragma once    
#include <iostream>
#include <string>
#include <unistd.h>  // getopt
#include <cstdlib>   // atoi


// 用來解析 <net file> <cell file> <output file>
class ArgumentParser {
public:
    std::string inputFile, outputFile;

    bool argparse(int argc, char* argv[]) {
        int opt;
        // 允許"-h" 參數: 印出用法
        while ((opt = getopt(argc, argv, "h")) != -1) {
            switch (opt) {
                case 'h':
                default:
                    printUsage(argv[0]);
                    return false;
            }
        }

        // 預期剩下 2 個位置參數
        // optind 是 getopt 的全域變數，指向"第一個非選項參數"的位置
        if (argc - optind != 2) {
            printUsage(argv[0]);
            return false;
        }

        inputFile = argv[optind];
        outputFile = argv[optind + 1];
        return true;
    }

    // 印出用法
    static void printUsage(const char* prog_name) {
        std::cerr << "Usage: " << prog_name << " <input txt file> <output txt file> \n"
                  << "Example:\n"
                  << "    " << prog_name << " ../testcase/proj_3_global_routing_inputs_v2.txt ../output/proj_3_global_routing_outputs_v2.txt\n";
    }
};
