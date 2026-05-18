// ./OutputWriter/OutputWriter.cpp
#include "OutputWriter.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void writePoint(std::ofstream& out, const Point& p) {
    out << "(" << p.x << "," << p.y << ")";
}

void writePointList(std::ofstream& out, const std::vector<Point>& points) {
    for (int i = 0; i < static_cast<int>(points.size()); i++) {
        writePoint(out, points[i]);

        if (i + 1 < static_cast<int>(points.size())) {
            out << ", ";
        }
    }
}

void writeLayerAssignedSegments(std::ofstream& out, const Net& net) {
    out << net.name << "\n";

    for (const LayerAssignedSegment& segment : net.layerAssignedSegments) {
        out << "  " << segment.layerName << " ";
        writePoint(out, segment.start);
        out << " ";
        writePoint(out, segment.end);
        out << "\n";
    }
}

void writeLayerEdgeRecord(
    std::ofstream& out,
    const RoutingDB& db,
    const Edge& edge, 
    int layerIdx,
    bool horizontal,
    int x,
    int y
) {
    const int x2 = horizontal ? x + 1 : x;
    const int y2 = horizontal ? y : y + 1;

    out << "EDGE "
        << db.layers[layerIdx].name << " "
        << (horizontal ? "H" : "V") << " "
        << x << " " << y << " "
        << x2 << " " << y2 << " "
        << edge.layerCapacity[layerIdx] << " "
        << edge.layerUsage[layerIdx] << "\n";
}

} // namespace

void writeRoutingOutput(const RoutingDB& db, const std::string& outputFile) {
    std::ofstream out(outputFile);

    if (!out) {
        throw std::runtime_error("[OutputWriter Error] Cannot open output file: " + outputFile);
    }

    out << "Gcell_grid  " << db.cols << " " << db.rows << "\n";

    for (const Net& net : db.nets) {
        if (!net.layerAssignedSegments.empty()) {
            writeLayerAssignedSegments(out, net);
            continue;
        }

        out << net.name << " ";

        if (!net.orderedPoints.empty()) {
            writePointList(out, net.orderedPoints);
        }

        out << "\n";
    }

    std::cout << "[OutputWriter] Routing result written to "
              << outputFile << "\n";
}

void writeCongestionMap(const RoutingDB& db, const std::string& outputFile) {
    std::ofstream out(outputFile);

    if (!out) {
        throw std::runtime_error("[OutputWriter Error] Cannot open congestion map file: " + outputFile);
    }

    out << "# Congestion map format\n";
    out << "# GRID <cols> <rows>\n";
    out << "# LAYER <layer_name> <preferred_direction>\n";
    out << "# EDGE <layer_name> <orientation:H|V> <x1> <y1> <x2> <y2> <capacity> <usage>\n";
    out << "#\n";
    out << "# Notes:\n";
    out << "# 1. Each EDGE line describes one unit edge on one routing layer.\n";
    out << "# 2. capacity/usage here are per-layer values, which are enough for later visualization.\n";
    out << "# 3. Horizontal edges connect (x,y) to (x+1,y); vertical edges connect (x,y) to (x,y+1).\n";
    out << "\n";

    out << "GRID " << db.cols << " " << db.rows << "\n";

    for (int i = 0; i < static_cast<int>(db.layers.size()); i++) {
        out << "LAYER " << db.layers[i].name << " " << db.layers[i].preferredDirection << "\n";
    }

    out << "\n";

    for (int layerIdx = 0; layerIdx < static_cast<int>(db.layers.size()); layerIdx++) {
        for (int x = 0; x < db.cols - 1; x++) {
            for (int y = 0; y < db.rows; y++) {
                writeLayerEdgeRecord(out, db, db.hEdges[x][y], layerIdx, true, x, y);
            }
        }

        for (int x = 0; x < db.cols; x++) {
            for (int y = 0; y < db.rows - 1; y++) {
                writeLayerEdgeRecord(out, db, db.vEdges[x][y], layerIdx, false, x, y);
            }
        }
    }

    std::cout << "[OutputWriter] Congestion map written to "
              << outputFile << "\n";
}
