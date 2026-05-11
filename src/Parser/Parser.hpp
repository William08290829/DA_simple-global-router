#pragma once

#include <string>
#include "Structure.hpp"



RoutingDB parseInputFile(const std::string& filename);

void testParseInputFile(const RoutingDB& db);