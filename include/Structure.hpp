#pragma once
// Structure.hpp

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

struct Point {
    // Represents a point in the grid
    // pin is a point where a net can connect
    // but Point is not necessarily a pin, it can be any point in the grid

    int x, y;
    Point(int x = 0, int y = 0) : x(x), y(y) {}

    // for std::map/set
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator<(const Point& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

struct Pin : public Point {
    // Represents a pin, which is a specific type of point where a net can connect
    Pin(int x = 0, int y = 0) : Point(x, y) {}
};

struct LayerAssignedSegment {
    std::string layerName;
    Point start;
    Point end;
};

struct Net {
    int id; // Unique identifier for the net
    std::string name;
    std::vector<Pin> pins;

    // For routing results
    std::vector<std::vector<Point>> routedPaths;

    // traversal order
    std::vector<Point> orderedPoints;

    // layer assignment result after 2D routing
    std::vector<LayerAssignedSegment> layerAssignedSegments;
};

struct Edge {
    int capacity = 0;
    int usage = 0;
    int historyCost = 0; // for history-based cost
    bool initialized = false;   // Indicates whether the edge has been initialized with capacity
    std::unordered_set<int> netIds;  // IDs of nets using this edge

    std::vector<int> layerCapacity;  // [layerIdx] is the capacity of this edge on layer layerIdx
    std::vector<int> layerUsage;     // [layerIdx] is the usage of this edge on layer layerIdx
};

// struct GCell {
//     int capacity = 0;
//     int usage = 0;
//     std::vector<int> netIds;  // IDs of nets using this cell
// };

struct Layer {
    std::string name;
    std::string preferredDirection; // "horizontal" or "vertical"
};

struct RoutingDB {
    int cols = 0;
    int rows = 0;

    std::vector<Layer> layers;
    std::map<std::string, std::string> layerPref; // M1 -> horizontal, M2 -> vertical
    std::unordered_map<std::string, int> layerNameToIdx;    // M1 -> 0, M2 -> 1
    // cells[x][y]
    // std::vector<std::vector<GCell>> cells;

    // hEdges[x][y] means edge between (x, y) and (x+1, y)
    std::vector<std::vector<Edge>> hEdges;

    // vEdges[x][y] means edge between (x, y) and (x, y+1)
    std::vector<std::vector<Edge>> vEdges;

    std::vector<Net> nets;
};
