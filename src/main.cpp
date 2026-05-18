#include <iostream>
#include "Structure.hpp"
#include "Timer/Timer.hpp"
#include "Parser/ArgumentParser.hpp"
#include "Parser/Parser.hpp"
#include "Router/FluteRouter.hpp"
#include "Router/Router.hpp"
#include "Router/LayerAssignment.hpp"
#include "OutputWriter/OutputWriter.hpp"

int main(int argc, char* argv[]) {
    Timer timer;
    timer.startTotalTimer();
    ArgumentParser argParser;
    if (!argParser.argparse(argc, argv)) {
        return 1;
    }

    std::cout << "================================" << std::endl;
    std::cout << "Project Name: Simple Global Router" << std::endl;
    std::cout << "Input File: " << argParser.inputFile << "\n";
    std::cout << "Output File: " << argParser.outputFile << "\n";
    if (!argParser.congestionMapFile.empty()) {
        std::cout << "Congestion Map File: " << argParser.congestionMapFile << "\n";
    }
    std::cout << "Group: 6\n";
    std::cout << "================================" << std::endl;
    std::cout << "\n\n";

    std::cout << "[Main] Starting routing process...\n";
    
    try {
        // Parse input file
        timer.startTimer("Parse");
        RoutingDB db = parseInputFile(argParser.inputFile);
        // testParseInputFile(db);
        timer.stopTimer("Parse");

        // Route nets
        timer.startTimer("Route");
        routeAllNetsByFlute(db);
        int maxIterations = 10;
        RipupAndReroute(db, maxIterations);
        assignLayers(db);
        timer.stopTimer("Route");

        // Output results
        timer.startTimer("Output");
        writeRoutingOutput(db, argParser.outputFile);
        if (!argParser.congestionMapFile.empty()) {
            writeCongestionMap(db, argParser.congestionMapFile);
        }
        timer.stopTimer("Output");
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[Main] Routing completed successfully.\n";

    timer.stopTotalTimer();
    timer.printAllTimers();
    std::cout << "\n[Main] Output file written to: " << argParser.outputFile << std::endl;
    if (!argParser.congestionMapFile.empty()) {
        std::cout << "[Main] Congestion map written to: " << argParser.congestionMapFile << std::endl;
    }
    return 0;
}
