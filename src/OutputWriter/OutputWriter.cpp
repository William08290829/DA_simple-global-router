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

} // namespace

void writeRoutingOutput(const RoutingDB& db, const std::string& outputFile) {
    std::ofstream out(outputFile);

    if (!out) {
        throw std::runtime_error("[OutputWriter Error] Cannot open output file: " + outputFile);
    }

    out << "Gcell_grid  " << db.cols << " " << db.rows << "\n";

    for (const Net& net : db.nets) {
        out << net.name << " ";

        if (!net.orderedPoints.empty()) {
            writePointList(out, net.orderedPoints);
        }

        out << "\n";
    }

    std::cout << "[OutputWriter] Routing result written to "
              << outputFile << "\n";
}
