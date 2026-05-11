#include "FluteRouter.hpp"

#include <iostream>
#include <vector>
#include <cstdlib>  // for std::abs
#include <cmath> // for std::exp
#include <set>
#include <algorithm> // for std::min, std::max, std::find
#include <stdexcept> // for std::runtime_error

extern "C" {
#include "flute.h"
}

// flute.h 會定義 min/max/abs macro，可能會污染 C++
// 保險起見 include 後把它們 undef 掉
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#ifdef abs
#undef abs
#endif

namespace {

bool fluteInitialized = false;

void initializeFluteOnce() {
    if (!fluteInitialized) {
        readLUT();
        fluteInitialized = true;
    }
}

void appendPoint(
    std::vector<Point>& path,
    int x,
    int y
) {
    if (!path.empty() && path.back().x == x && path.back().y == y) {
        return;
    }

    path.emplace_back(x, y);
}

void addNetToEdge(Edge& edge, int netId) {
    auto [it, inserted] = edge.netIds.insert(netId);
    if (inserted) {
        edge.usage++;
    }
}

void routeHorizontalSegment(
    RoutingDB& db,
    int y,
    int x1,
    int x2,
    int netId,
    std::vector<Point>& path
) {
    if (x1 == x2) {
        appendPoint(path, x1, y);
        return;
    }

    int step = (x2 > x1) ? 1 : -1;
    appendPoint(path, x1, y);

    for (int x = x1; x != x2; x += step) {
        int nextX = x + step;
        int edgeX = std::min(x, nextX); // edgeX 是兩個點中較小的 x 座標，對應到 hEdges 的索引

        if (edgeX < 0 || edgeX >= db.cols - 1 || y < 0 || y >= db.rows) {
            throw std::runtime_error("[Router Error] Horizontal segment out of grid");
        }

        addNetToEdge(db.hEdges[edgeX][y], netId);
        appendPoint(path, nextX, y);
    }
}

void routeVerticalSegment(
    RoutingDB& db,
    int x,
    int y1,
    int y2,
    int netId,
    std::vector<Point>& path
) {
    if (y1 == y2) {
        appendPoint(path, x, y1);
        return;
    }

    int step = (y2 > y1) ? 1 : -1;
    appendPoint(path, x, y1);

    for (int y = y1; y != y2; y += step) {
        int nextY = y + step;
        int edgeY = std::min(y, nextY); // edgeY 是兩個點中較小的 y 座標，對應到 vEdges 的索引

        if (x < 0 || x >= db.cols || edgeY < 0 || edgeY >= db.rows - 1) {
            throw std::runtime_error("[FLUTE Router Error] Vertical segment out of grid");
        }

        addNetToEdge(db.vEdges[x][edgeY], netId);
        appendPoint(path, x, nextY);
    }
}

double costFunction(int usage, int capacity, double a = 10.0, int b = 5.0) {
    if (capacity <= 0) {
        return 1e9; // 無限大成本，表示無法使用
    }

    return 1.0 + a / (1.0 + std::exp(-b * (usage - capacity))); 
}

double getHorizontalSegmentCost(
    const RoutingDB& db,
    int y,
    int x1,
    int x2
) {
    double cost = 0.0;

    int start = std::min(x1, x2);
    int end = std::max(x1, x2);

    for (int x = start; x < end; x++) {
        const Edge& e = db.hEdges[x][y];

        int nextUsage = e.usage + 1;

        cost += costFunction(nextUsage, e.capacity);
    }

    return cost;
}

double getVerticalSegmentCost(
    const RoutingDB& db,
    int x,
    int y1,
    int y2
) {
    double cost = 0.0;

    int start = std::min(y1, y2);
    int end = std::max(y1, y2);

    for (int y = start; y < end; y++) {
        const Edge& e = db.vEdges[x][y];

        int nextUsage = e.usage + 1;

        cost += costFunction(nextUsage, e.capacity);
    }

    return cost;
}

void routeBestLShapeSegment(
    RoutingDB& db,
    int x1,
    int y1,
    int x2,
    int y2,
    int netId
) {
    if (x1 == x2 && y1 == y2) {
        return;
    }

    std::vector<Point> path;

    if (x1 == x2) {
        routeVerticalSegment(db, x1, y1, y2, netId, path);
        db.nets[netId].routedPaths.push_back(path);
        return;
    }

    if (y1 == y2) {
        routeHorizontalSegment(db, y1, x1, x2, netId, path);
        db.nets[netId].routedPaths.push_back(path);
        return;
    }

    double hvCost =
        getHorizontalSegmentCost(db, y1, x1, x2) +
        getVerticalSegmentCost(db, x2, y1, y2);

    double vhCost =
        getVerticalSegmentCost(db, x1, y1, y2) +
        getHorizontalSegmentCost(db, y2, x1, x2);

    if (hvCost <= vhCost) {
        routeHorizontalSegment(db, y1, x1, x2, netId, path);
        routeVerticalSegment(db, x2, y1, y2, netId, path);
    } else {
        routeVerticalSegment(db, x1, y1, y2, netId, path);
        routeHorizontalSegment(db, y2, x1, x2, netId, path);
    }

    if (!path.empty()) {
        db.nets[netId].routedPaths.push_back(path);
    }
    
    return;
}

void routeFluteTreeBranches(
    RoutingDB& db,
    int netId,
    const Tree& tree
) {
    // FLUTE 的 branch 結構是以一個陣列來表示整棵樹，branch[i] 代表第 i 條 branch
    // branchCount 是 branch 的總數量，等於 2*degree - 2，其中 degree 是 net 的 pin 數量
    int branchCount = 2 * tree.deg - 2;

    std::set<std::pair<int, int>> visitedBranchEdges; // 用來記錄已經走過的 branch edge，避免重複走同一條 edge

    for (int i = 0; i < branchCount; i++) {
        int neighbor = tree.branch[i].n;

        if (neighbor < 0 || neighbor >= branchCount) {  // 避免 FLUTE tree 結構異常導致的越界訪問
            throw std::runtime_error("[FLUTE Router Error] Invalid neighbor index in FLUTE tree");
            continue;
        }

        if (neighbor == i) { // 自己連自己，可能是 FLUTE 的特殊情況，直接跳過
            continue;
        }

        int x1 = tree.branch[i].x;
        int y1 = tree.branch[i].y;
        int x2 = tree.branch[neighbor].x;
        int y2 = tree.branch[neighbor].y;

        if (x1 == x2 && y1 == y2) {
            continue;
        }

        int a = std::min(i, neighbor);
        int b = std::max(i, neighbor);
        if (visitedBranchEdges.count({a, b})) {
            continue;
        }
        visitedBranchEdges.insert({a, b});

        routeBestLShapeSegment(db, x1, y1, x2, y2, netId);
    }
}

std::map<Point, std::vector<Point>> buildAdjList(const Net& net) {
    std::map<Point, std::vector<Point>> list;

    for (const auto& path : net.routedPaths) {
        for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
            Point a = path[i];
            Point b = path[i + 1];

            list[a].push_back(b);
            list[b].push_back(a);
        }
    }

    return list;
}

    void dfsOrderedPoints(
        const Point& u,
        const std::map<Point, std::vector<Point>>& adjList,
        std::set<Point>& visited,
        std::vector<Point>& ordered
    ) {
        visited.insert(u);
        ordered.push_back(u);

        auto it = adjList.find(u);
        if (it == adjList.end()) {
            return;
        }

        std::vector<Point> neighbors = it->second;

        // 讓 DFS 的 traversal order 固定
        std::sort(neighbors.begin(), neighbors.end());

        for (const Point& v : neighbors) {
            if (visited.count(v)) {
                continue;
            }

            dfsOrderedPoints(v, adjList, visited, ordered);
        }
    }

    void buildOrderedPointsForNet(RoutingDB& db, int netId) {
        Net& net = db.nets[netId];

        net.orderedPoints.clear();

        auto list = buildAdjList(net);

        if (list.empty()) {
            return;
        }

        // 從第一個 pin 開始
        Point start(net.pins[0].x, net.pins[0].y);

        // 如果第一個 pin 不在 graph 裡，就從 graph 的第一個點開始
        if (list.find(start) == list.end()) {
            start = list.begin()->first;
        }

        std::set<Point> visited;
        dfsOrderedPoints(start, list, visited, net.orderedPoints);
    }

} // namespace


void routeOneNetByFlute(RoutingDB& db, const int netId) {
     if (netId < 0 || netId >= static_cast<int>(db.nets.size())) {
        throw std::runtime_error("[FLUTE Router Error] Invalid netId");
    }

    initializeFluteOnce();

    const Net& net = db.nets[netId];

    // FLUTE 的 degree 是指 net 的 pin 數量
    int degree = static_cast<int>(net.pins.size()); 

    if (degree <= 1) {  //  single-pin, no need to route
        return;
    }

    // FLUTE 的接口需要兩個平行陣列來存放 pin 的 x 和 y 座標
    std::vector<DTYPE> x(degree);
    std::vector<DTYPE> y(degree);

    for (int i = 0; i < degree; i++) {
        x[i] = net.pins[i].x;
        y[i] = net.pins[i].y;
    }

    // FLUTE 的 accuracy 參數控制路由的精確度
    int accuracy = 3;
    Tree tree = flute(degree, x.data(), y.data(), accuracy);

    std::cout << "[FLUTE Router] Net " << net.name
              << ", degree = " << degree
              << ", FLUTE length = " << tree.length
              << "\n";

    // output FLUTE tree structure for debugging
    // int branchCount = 2 * tree.deg - 2; // FLUTE 的 branch 數量是 2*degree - 2

    // std::cout << "Branches:\n";
    // for (int i = 0; i < branchCount; i++) {
    //     const Branch& b = tree.branch[i];

    //     std::cout << "  branch[" << i << "] = ("
    //               << b.x << ", " << b.y << "), neighbor = "
    //               << b.n << "\n";
    // }
    // output FLUTE tree structure for debugging


    routeFluteTreeBranches(db, netId, tree);

    buildOrderedPointsForNet(db, netId);

    // 釋放 FLUTE tree 結構的記憶體
    if (tree.branch != nullptr) {
        free(tree.branch);
    }
}

int computeTotalWirelength(const RoutingDB& db) {
    int wl = 0;

    for (int x = 0; x < db.cols - 1; x++) {
        for (int y = 0; y < db.rows; y++) {
            wl += db.hEdges[x][y].usage;
        }
    }

    for (int x = 0; x < db.cols; x++) {
        for (int y = 0; y < db.rows - 1; y++) {
            wl += db.vEdges[x][y].usage;
        }
    }

    return wl;
}

int computeTotalOverflow(const RoutingDB& db) {
    int overflow = 0;

    for (int x = 0; x < db.cols - 1; x++) {
        for (int y = 0; y < db.rows; y++) {
            const Edge& e = db.hEdges[x][y];
            overflow += std::max(0, e.usage - e.capacity);
        }
    }

    for (int x = 0; x < db.cols; x++) {
        for (int y = 0; y < db.rows - 1; y++) {
            const Edge& e = db.vEdges[x][y];
            overflow += std::max(0, e.usage - e.capacity);
        }
    }

    return overflow;
}

void printFluteRouterResults(const RoutingDB& db) {
    std::cout << "[FLUTE Router] FLUTE routing results:\n";

    // for (const auto& net : db.nets) {
    //     std::cout << "  Net " << net.name << ":\n";
    //     for (const auto& path : net.routedPaths) {
    //         std::cout << "    Path: ";
    //         for (const auto& point : path) {
    //             std::cout << "(" << point.x << "," << point.y << ") ";
    //         }
    //         std::cout << "\n";
    //     }
    //     std::cout << "\n";
    // }
    // std::cout << "\n";

    for (const auto& net : db.nets) {
        std::cout << "  Net " << net.name << " ordered points:\n    ";
        for (const auto& point : net.orderedPoints) {
            std::cout << "(" << point.x << "," << point.y << ") ";
        }
        std::cout << "\n\n";
    }

    return;
}

void routeAllNetsByFlute(RoutingDB& db) {
    std::cout << "[FLUTE Router] Start routing nets by FLUTE...\n";
    for (int netId = 0; netId < static_cast<int>(db.nets.size()); netId++) {
        routeOneNetByFlute(db, netId);
    }

    // printFluteRouterResults(db);

    // 計算並輸出總線長和總 overflow
    std::cout << "[FLUTE Router] Total wirelength = " << computeTotalWirelength(db) << "\n";
    std::cout << "[FLUTE Router] Total overflow = " << computeTotalOverflow(db) << "\n";
    std::cout << "[FLUTE Router] Finished routing nets by FLUTE.\n";
    std::cout << "\n";
    return;
}