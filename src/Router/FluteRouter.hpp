#pragma once

#include "Structure.hpp"


void routeAllNetsByFlute(RoutingDB& db);
void routeOneNetByFlute(RoutingDB& db, int netId);
void printFluteRouterResults(const RoutingDB& db);

int computeTotalWirelength(const RoutingDB& db);
int computeTotalOverflow(const RoutingDB& db);