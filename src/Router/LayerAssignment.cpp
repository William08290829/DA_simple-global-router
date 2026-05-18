#include "LayerAssignment.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct UnitEdgeRef {
    Point a;
    Point b;

    bool operator<(const UnitEdgeRef& other) const {
        if (a < other.a) {
            return true;
        }
        if (other.a < a) {
            return false;
        }
        return b < other.b;
    }
};

UnitEdgeRef normalizeEdge(const Point& p1, const Point& p2) {
    if (std::abs(p1.x - p2.x) + std::abs(p1.y - p2.y) != 1) {
        throw std::runtime_error("[LayerAssignment Error] Points are not adjacent");
    }

    if (p2 < p1) {
        return UnitEdgeRef{p2, p1};
    }

    return UnitEdgeRef{p1, p2};
}

bool isHorizontalEdge(const UnitEdgeRef& edge) {
    return edge.a.y == edge.b.y;
}

Edge& getEdgeFromUnitEdge(RoutingDB& db, const UnitEdgeRef& edge) {
    if (isHorizontalEdge(edge)) {
        return db.hEdges[edge.a.x][edge.a.y];
    }

    return db.vEdges[edge.a.x][edge.a.y];
}

std::map<Point, std::vector<Point>> buildAdjListUnique(const Net& net) {
    std::map<Point, std::set<Point>> uniqueAdj;

    for (const auto& path : net.routedPaths) {
        for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
            const Point& a = path[i];
            const Point& b = path[i + 1];
            uniqueAdj[a].insert(b);
            uniqueAdj[b].insert(a);
        }
    }

    std::map<Point, std::vector<Point>> adjList;
    for (const auto& item : uniqueAdj) {
        adjList[item.first] = std::vector<Point>(item.second.begin(), item.second.end());
    }

    return adjList;
}

std::vector<UnitEdgeRef> collectUniqueEdges(const Net& net) {
    std::set<UnitEdgeRef> uniqueEdges;

    for (const auto& path : net.routedPaths) {
        for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
            uniqueEdges.insert(normalizeEdge(path[i], path[i + 1]));
        }
    }

    return std::vector<UnitEdgeRef>(uniqueEdges.begin(), uniqueEdges.end());
}

int chooseLayerForEdge(const RoutingDB& db, const Edge& edge, bool horizontal) {
    int bestLayerIdx = -1;
    int bestRemaining = -1;

    for (int layerIdx = 0; layerIdx < static_cast<int>(db.layers.size()); layerIdx++) {
        const Layer& layer = db.layers[layerIdx];
        const bool directionMatch =
            (horizontal && layer.preferredDirection == "horizontal") ||
            (!horizontal && layer.preferredDirection == "vertical");

        if (!directionMatch) {
            continue;
        }

        int remaining = edge.layerCapacity[layerIdx] - edge.layerUsage[layerIdx];
        if (remaining <= 0) {
            continue;
        }

        if (remaining > bestRemaining) {
            bestRemaining = remaining;
            bestLayerIdx = layerIdx;
        }
    }

    return bestLayerIdx;
}

void assignLayersForNet(
    RoutingDB& db,
    Net& net,
    std::map<UnitEdgeRef, int>& edgeToLayer
) {
    const std::vector<UnitEdgeRef> uniqueEdges = collectUniqueEdges(net);

    for (const UnitEdgeRef& edgeRef : uniqueEdges) {
        Edge& edge = getEdgeFromUnitEdge(db, edgeRef);
        const bool horizontal = isHorizontalEdge(edgeRef);
        const int layerIdx = chooseLayerForEdge(db, edge, horizontal);

        if (layerIdx < 0) {
            throw std::runtime_error(
                "[LayerAssignment Error] No legal layer left for net " + net.name +
                " on edge (" + std::to_string(edgeRef.a.x) + "," + std::to_string(edgeRef.a.y) +
                ")-(" + std::to_string(edgeRef.b.x) + "," + std::to_string(edgeRef.b.y) + ")"
            );
        }

        edge.layerUsage[layerIdx]++;
        edgeToLayer[edgeRef] = layerIdx;
    }
}

void buildSegmentsDfs(
    const Point& current,
    const std::map<Point, std::vector<Point>>& adjList,
    std::set<Point>& visitedNodes,
    std::set<UnitEdgeRef>& visitedEdges,
    const std::map<UnitEdgeRef, int>& edgeToLayer,
    const RoutingDB& db,
    Net& net
) {
    visitedNodes.insert(current);

    auto it = adjList.find(current);
    if (it == adjList.end()) {
        return;
    }

    std::vector<Point> neighbors = it->second;
    std::sort(neighbors.begin(), neighbors.end());

    for (const Point& next : neighbors) {
        UnitEdgeRef edgeRef = normalizeEdge(current, next);
        if (visitedEdges.count(edgeRef)) {
            continue;
        }

        auto assignIt = edgeToLayer.find(edgeRef);
        if (assignIt == edgeToLayer.end()) {
            throw std::runtime_error("[LayerAssignment Error] Missing assigned layer for routed edge");
        }

        visitedEdges.insert(edgeRef);
        net.layerAssignedSegments.push_back(
            LayerAssignedSegment{
                db.layers[assignIt->second].name,
                current,
                next
            }
        );

        if (!visitedNodes.count(next)) {
            buildSegmentsDfs(next, adjList, visitedNodes, visitedEdges, edgeToLayer, db, net);
        }
    }
}

void buildAssignedSegmentsForNet(RoutingDB& db, Net& net, const std::map<UnitEdgeRef, int>& edgeToLayer) {
    net.layerAssignedSegments.clear();

    const auto adjList = buildAdjListUnique(net);
    if (adjList.empty()) {
        return;
    }

    Point start(net.pins[0].x, net.pins[0].y);
    if (adjList.find(start) == adjList.end()) {
        start = adjList.begin()->first;
    }

    std::set<Point> visitedNodes;
    std::set<UnitEdgeRef> visitedEdges;
    buildSegmentsDfs(start, adjList, visitedNodes, visitedEdges, edgeToLayer, db, net);
}

void resetLayerUsage(RoutingDB& db) {
    for (int x = 0; x < db.cols - 1; x++) {
        for (int y = 0; y < db.rows; y++) {
            std::fill(db.hEdges[x][y].layerUsage.begin(), db.hEdges[x][y].layerUsage.end(), 0);
        }
    }

    for (int x = 0; x < db.cols; x++) {
        for (int y = 0; y < db.rows - 1; y++) {
            std::fill(db.vEdges[x][y].layerUsage.begin(), db.vEdges[x][y].layerUsage.end(), 0);
        }
    }
}

} // namespace

void assignLayers(RoutingDB& db) {
    resetLayerUsage(db);

    for (Net& net : db.nets) {
        std::map<UnitEdgeRef, int> edgeToLayer;
        assignLayersForNet(db, net, edgeToLayer);
        buildAssignedSegmentsForNet(db, net, edgeToLayer);
    }
}
