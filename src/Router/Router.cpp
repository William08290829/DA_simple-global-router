// ./Router/Router.cpp
#include "Router.hpp"
#include "Structure.hpp"

#include <random>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>

std::mt19937 rng(24);

namespace {

constexpr int INF_INT = std::numeric_limits<int>::max();
constexpr double INF_DOUBLE = std::numeric_limits<double>::max();

struct EdgeRef {
    bool horizontal; // true: hEdges, false: vEdges
    int x;
    int y;

    bool operator==(const EdgeRef& other) const {
        return horizontal == other.horizontal &&
               x == other.x &&
               y == other.y;
    }

    bool operator<(const EdgeRef& other) const {
        if (horizontal != other.horizontal) return horizontal < other.horizontal;
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

struct BranchCandidate {
    int netId = -1;
    int pathIdx = -1;
};

EdgeRef getEdgeRefFromAdjPoints(const Point& a, const Point& b) {
    // 根據兩個相鄰的點 a 和 b，計算出對應的邊（EdgeRef）
    int dx = b.x - a.x;
    int dy = b.y - a.y;

    if (std::abs(dx) + std::abs(dy) != 1) {
        throw std::runtime_error("[Router Error] Points are not adjacent");
    }

    if (dx != 0) {
        return EdgeRef{ true, std::min(a.x, b.x), a.y };
    }

    return EdgeRef{ false, a.x, std::min(a.y, b.y) };
}

Edge& getEdge(RoutingDB& db, const EdgeRef& ref) {
    if (ref.horizontal) {
        return db.hEdges[ref.x][ref.y];
    }

    return db.vEdges[ref.x][ref.y];
}

const Edge& getEdgeConst(const RoutingDB& db, const EdgeRef& ref) {
    if (ref.horizontal) {
        return db.hEdges[ref.x][ref.y];
    }

    return db.vEdges[ref.x][ref.y];
}

std::vector<EdgeRef> pathToEdges(const std::vector<Point>& path) {
    std::vector<EdgeRef> edges;

    for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
        edges.push_back(getEdgeRefFromAdjPoints(path[i], path[i + 1]));
    }

    return edges;
}

void addNetToEdge(Edge& edge, int netId) {
    auto [it, inserted] = edge.netIds.insert(netId);
    if (inserted) {
        edge.usage++;
    }
}

void addPathToEdges(RoutingDB& db, const std::vector<Point>& path, int netId) {
    std::vector<EdgeRef> edges = pathToEdges(path);

    for (const EdgeRef& ref : edges) {
        addNetToEdge(getEdge(db, ref), netId);
    }
}

bool pathUsesEdge( const std::vector<Point>& path, const EdgeRef& target ) {
    // 檢查給定的 path 是否使用了 target 這條邊
    std::vector<EdgeRef> edges = pathToEdges(path);

    for (const EdgeRef& e : edges) {
        if (e == target) {
            return true;
        }
    }

    return false;
}

bool netStillUsesEdge( const Net& net, const EdgeRef& target ) {
    for (const auto& path : net.routedPaths) {
        if (pathUsesEdge(path, target)) {
            return true;
        }
    }

    return false;
}

void removePathFromEdges( RoutingDB& db, const std::vector<Point>& removedPath, int netId ) {
    Net& net = db.nets[netId];

    std::vector<EdgeRef> edges = pathToEdges(removedPath);

    // 避免同一條 path 裡同一條 edge 出現多次時重複處理
    std::set<EdgeRef> uniqueEdges(edges.begin(), edges.end());

    for (const EdgeRef& ref : uniqueEdges) {
        // 如果這條 net 的其他 path 還有用這條 edge，就不能移除 netId
        if (netStillUsesEdge(net, ref)) {
            continue;
        }

        Edge& edge = getEdge(db, ref);
        edge.netIds.erase(netId);
        edge.usage = static_cast<int>(edge.netIds.size());
    }
}

std::vector<EdgeRef> findOverflowEdges(RoutingDB& db) {
    std::vector<EdgeRef> result;

    for (int x = 0; x < db.cols - 1; x++) {
        for (int y = 0; y < db.rows; y++) {
            Edge& e = db.hEdges[x][y];

            if (e.usage > e.capacity) {
                e.historyCost++;  // 每次被檢查到 overflow 就增加 history cost
                result.push_back(EdgeRef{true, x, y});
            }
        }
    }

    for (int x = 0; x < db.cols; x++) {
        for (int y = 0; y < db.rows - 1; y++) {
            Edge& e = db.vEdges[x][y];

            if (e.usage > e.capacity) {
                e.historyCost++;  // 每次被檢查到 overflow 就增加 history cost
                result.push_back(EdgeRef{false, x, y});
            }
        }
    }

    return result;
}

int computeTotalOverflow(const RoutingDB& db) {
    int total = 0;

    for (int x = 0; x < db.cols - 1; x++) {
        for (int y = 0; y < db.rows; y++) {
            const Edge& e = db.hEdges[x][y];
            total += std::max(0, e.usage - e.capacity);
        }
    }

    for (int x = 0; x < db.cols; x++) {
        for (int y = 0; y < db.rows - 1; y++) {
            const Edge& e = db.vEdges[x][y];
            total += std::max(0, e.usage - e.capacity);
        }
    }

    return total;
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

std::vector<BranchCandidate> findBranchesUsingOverflowEdge( const RoutingDB& db, const EdgeRef& overflowEdge ) {
    // 找到所有使用 overflowEdge 的 branch candidate，回傳 netId 和 pathIdx
    const Edge& edge = getEdgeConst(db, overflowEdge);
    std::vector<BranchCandidate> candidates;

    for (int netId : edge.netIds) {
        const Net& net = db.nets[netId];

        for (int pathIdx = 0; pathIdx < static_cast<int>(net.routedPaths.size()); pathIdx++) {
            if (pathUsesEdge(net.routedPaths[pathIdx], overflowEdge)) {
                candidates.push_back(BranchCandidate{netId, pathIdx});
            }
        }
    }

    return candidates;
}

std::map<Point, std::vector<Point>> buildAdjList(const Net& net) {
    // 根據 net 的 routedPaths 建立一個AdjList，這個 AdjList 的 key 是 Point，value 是所有與該 Point 相鄰的 Point
    std::map<Point, std::vector<Point>> graph;

    for (const auto& path : net.routedPaths) {
        for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
            Point a = path[i];
            Point b = path[i + 1];

            graph[a].push_back(b);
            graph[b].push_back(a);
        }
    }

    return graph;
}

std::vector<Point> getComponentNodes(
    const std::map<Point, std::vector<Point>>& graph,
    const Point& start
) {
    // 用 BFS 從 graph 的 start 點開始，找到所有與 start 連通的點，這些點組成一個 component
    std::vector<Point> component;
    std::set<Point> visited;
    std::queue<Point> q;

    if (graph.find(start) == graph.end()) {
        component.push_back(start);
        return component;
    }

    visited.insert(start);
    q.push(start);

    while (!q.empty()) {
        Point u = q.front();
        q.pop();

        component.push_back(u);

        auto it = graph.find(u);
        if (it == graph.end()) {
            continue;
        }

        for (const Point& v : it->second) {
            if (!visited.count(v)) {
                visited.insert(v);
                q.push(v);
            }
        }
    }

    return component;
}

bool sameComponent( const std::vector<Point>& component, const Point& p ) {
    return std::find(component.begin(), component.end(), p) != component.end();
}

// Multi-source multi-sink Dijkstra's algorithm
bool inGrid(const RoutingDB& db, int x, int y) {
    return x >= 0 && x < db.cols && y >= 0 && y < db.rows;
}

// double getEdgeCost( const RoutingDB& db, const Point& a, const Point& b, int netId ) {
//     EdgeRef ref = getEdgeRefFromAdjPoints(a, b);
//     const Edge& e = getEdgeConst(db, ref);

//     bool alreadyUsedByThisNet = e.netIds.count(netId) > 0;
//     if (!alreadyUsedByThisNet && e.usage + 1 > e.capacity) {
//         return INF_DOUBLE;
//     }
    
//     if (e.capacity <= 0) {
//         return INF_DOUBLE;
//     }

//     int k1 = 10;
//     int his = e.historyCost;


//     if (e.usage + 1 > e.capacity) {
//         his++;  
//     }

//     if (his == 0) {
//         his = 1;
//     }

//     double penalty = std::pow(static_cast<double>(e.usage + 1) / e.capacity, k1);

//     return 1 + his * penalty;
// }
double getEdgeCost(const RoutingDB& db, const Point& a, const Point& b, int netId) {
    EdgeRef ref = getEdgeRefFromAdjPoints(a, b);
    const Edge& e = getEdgeConst(db, ref);

    if (e.capacity <= 0) {
        return INF_DOUBLE;
    }

    bool alreadyUsedByThisNet = e.netIds.count(netId) > 0;

    if (!alreadyUsedByThisNet && e.usage + 1 > e.capacity) {
        return INF_DOUBLE;
    }

    return 1.0;
}

std::vector<Point> multiSourceMultiSinkDijkstra( const RoutingDB& db, const std::vector<Point>& sources, const std::vector<Point>& sinks, int netId ) {
    // dist[x][y] = 從 sources 中任一點走到 (x, y) 的最短距離
    std::vector<std::vector<double>> dist( db.cols, std::vector<double>(db.rows, INF_DOUBLE) );

    // parent[x][y] = 從 sources 中任一點走到 (x, y) 的最短路徑上，(x, y) 的前一個點是 parent[x][y]
    std::vector<std::vector<Point>> parent( db.cols, std::vector<Point>(db.rows, Point(-1, -1)) );

    // 將 sinks 放入一個 set 中，方便在 Dijkstra 的過程中快速判斷當前點是否為 sink
    std::set<Point> sinkSet(sinks.begin(), sinks.end());

    // State = (cost, point)，cost 是從 sources 中任一點走到 point 的最短距離，point 是當前點
    using State = std::pair<double, Point>;

    auto cmp = [](const State& a, const State& b) {
        return a.first > b.first;
    };

    std::priority_queue<State, std::vector<State>, decltype(cmp)> pq(cmp);

    // 將所有 sources 的距離初始化為 0，並加入priority queue
    for (const Point& s : sources) {
        if (!inGrid(db, s.x, s.y)) continue;

        dist[s.x][s.y] = 0;
        pq.push({0, s});
    }

    Point target(-1, -1);   // 最終會找到一個 sink 作為 target，然後從 target 回溯到某個 source 得到路徑

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (!pq.empty()) {
        auto [cost, u] = pq.top();
        pq.pop();

        if (cost > dist[u.x][u.y]) {
            continue;
        }

        if (sinkSet.count(u)) { // if u is a sink
            target = u;
            break;
        }

        for (int dir = 0; dir < 4; dir++) {
            Point v(u.x + dx[dir], u.y + dy[dir]);

            if (!inGrid(db, v.x, v.y)) {
                continue;
            }

            double edgeCost = getEdgeCost(db, u, v, netId);

            if (edgeCost >= INF_DOUBLE) {
                continue;
            }

            double newCost = cost + edgeCost;

            if (newCost < dist[v.x][v.y]) {
                dist[v.x][v.y] = newCost;
                parent[v.x][v.y] = u;
                pq.push({newCost, v});
            }
        }
    }

    if (target.x == -1) {   // 沒有任何 sink 可連到
        return {};
    }

    std::vector<Point> path;

    Point cur = target;

    while (cur.x != -1) {
        path.push_back(cur);
        Point p = parent[cur.x][cur.y];
        cur = p;
    }

    std::reverse(path.begin(), path.end()); // 最後反轉 path

    return path;
}

bool OneRipupAndReroute(RoutingDB& db, const EdgeRef& overflowEdge) {
    std::vector<BranchCandidate> candidates = findBranchesUsingOverflowEdge(db, overflowEdge);

    if (candidates.empty()) {
        return false;
    }

    // random shuffle candidates to avoid deterministic behavior
    std::shuffle(candidates.begin(), candidates.end(), rng);

    bool anySuccess = false;

    for (const BranchCandidate& c : candidates) {

        Net& net = db.nets[c.netId];

        // 檢查 candidate 的合法性，確保 pathIdx 沒有越界
        if (c.pathIdx < 0 || c.pathIdx >= static_cast<int>(net.routedPaths.size())) {
            continue;
        }

        std::vector<Point> oldPath = net.routedPaths[c.pathIdx];
        if (oldPath.size() < 2) {   // path 太短，沒有 edge，跳過
            continue;
        }

        // 檢查 candidate 的合法性，確保這條 path 確實使用了 overflowEdge
        if (!pathUsesEdge(oldPath, overflowEdge)) {
            continue;
        }

        Point oldStart = oldPath.front();
        Point oldEnd = oldPath.back();

        // 1. rip up: 先移除舊 branch
        net.routedPaths.erase(net.routedPaths.begin() + c.pathIdx);
        // 2. 更新 usage / orderedPoints
        removePathFromEdges(db, oldPath, c.netId);
        // 3. 用剩下的 tree 建 graph
        auto graph = buildAdjList(net);
        // 4. 找被拆掉 branch 兩端所在的 component
        std::vector<Point> sourceComponent = getComponentNodes(graph, oldStart);
        std::vector<Point> sinkComponent = getComponentNodes(graph, oldEnd);

        // 如果 oldStart / oldEnd 是 leaf，可能不在 graph 裡，要補回來
        if (sourceComponent.empty()) {
            sourceComponent.push_back(oldStart);
        }
        if (sinkComponent.empty()) {
            sinkComponent.push_back(oldEnd);
        }

        // 如果兩端其實仍在同一個 component，代表這條 branch 可能是多餘的
        if (sameComponent(sourceComponent, oldEnd)) {
            anySuccess = true;
            continue;
        }

        // 5. multi-source multi-sink Dijkstra
        std::vector<Point> newPath = multiSourceMultiSinkDijkstra( db, sourceComponent, sinkComponent, c.netId );

        // 6. 如果找不到不 overflow 的路，就還原
        if (newPath.empty()) {
            net.routedPaths.insert(net.routedPaths.begin() + c.pathIdx, oldPath);

            addPathToEdges(db, oldPath, c.netId);

            continue;
        }

        // 7. 成功: 加入新的 path
        net.routedPaths.insert( net.routedPaths.begin() + c.pathIdx, newPath);

        // 8. 更新 usage / orderedPoints
        addPathToEdges(db, newPath, c.netId);

        std::cout << "[Router] Rerouted net " << net.name
                << " path " << c.pathIdx
                << ", old length = " << oldPath.size() - 1
                << ", new length = " << newPath.size() - 1
                << "\n";

        anySuccess = true;
    }
    return anySuccess;
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

}   // namespace

void RipupAndReroute( RoutingDB& db, int maxIterations ) {

    int overflow = computeTotalOverflow(db);
    if (overflow == 0) {
        std::cout << "[Router] No overflow, no need to rip-up and reroute.\n\n";
        return;
    }

    std::cout << "[Router] Starting rip-up and reroute...\n";
    std::cout << "[Router] Initial overflow = " << overflow << "\n";

    int iteration = 1;
    while (iteration <= maxIterations && overflow > 0) {
        std::vector<EdgeRef> overflowEdges = findOverflowEdges(db);

        if (overflowEdges.empty()) {
            std::cout << "[Router] No overflow after "
                      << iteration << " iterations.\n";
            break;
        }

        // 選 overflow 最大的 edge
        EdgeRef target = overflowEdges[0];
        int bestOverflow = -1;

        for (const EdgeRef& e : overflowEdges) {
            const Edge& edge = getEdgeConst(db, e);
            int overflow = edge.usage - edge.capacity;

            if (overflow > bestOverflow) {
                bestOverflow = overflow;
                target = e;
            }
        }

        std::cout << "[Router] Iteration " << iteration
                  << ", target overflow edge = "
                  << (target.horizontal ? "H" : "V")
                  << "(" << target.x << "," << target.y << ")"
                  << ", overflow = " << bestOverflow
                  << "\n";

        bool anySuccess = false;

        for (const EdgeRef& target : overflowEdges) {
            bool success = OneRipupAndReroute(db, target);

            if (success) {
                anySuccess = true;
                break;
            }
        }

        if (!anySuccess) {
            std::cout << "[Router] No reroutable overflow edge found.\n";
            break;
        }

        overflow = computeTotalOverflow(db);

        std::cout << "[Router] Current overflow = " << overflow << "\n";
        std::cout << "[Router] Current Wirelength = " << computeTotalWirelength(db) << "\n\n";

        iteration++;
    }

    for (int netId = 0; netId < static_cast<int>(db.nets.size()); netId++) {
        buildOrderedPointsForNet(db, netId);
    }


    std::cout << "[Router] Final overflow = "
              << computeTotalOverflow(db) << "\n";
    std::cout << "[Router] Final Wirelength = "
              << computeTotalWirelength(db) << "\n\n";
}