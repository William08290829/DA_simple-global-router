#pragma once

#include <string>
#include "Structure.hpp"

void writeRoutingOutput(const RoutingDB& db, const std::string& outputFile);
void writeCongestionMap(const RoutingDB& db, const std::string& outputFile);
