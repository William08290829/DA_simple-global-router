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

// negotiation-based rip-up and reroute 的參數
constexpr double kHistoryWeight = 1.0;          // history cost 的權重
constexpr double kInitialPresentFactor = 0.5;   // 第一輪 overflow 的懲罰倍率
constexpr double kPresentFactorGrowth = 1.5;    // 每輪懲罰倍率成長的比例
constexpr double kMaxPresentFactor = 1e6;
constexpr int kMaxStagnantIterations = 30;      // 連續這麼多輪沒有進步就停止

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
    auto insertResult = edge.netIds.insert(netId);
    bool inserted = insertResult.second;
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

int findPathUsingEdge( const Net& net, const EdgeRef& target ) {
    // 回傳 net 裡使用 target 這條邊的 path index，找不到回傳 -1
    // normalizeNetPaths 之後，每條邊只會屬於一條 path
    for (int pathIdx = 0; pathIdx < static_cast<int>(net.routedPaths.size()); pathIdx++) {
        if (pathUsesEdge(net.routedPaths[pathIdx], target)) {
            return pathIdx;
        }
    }

    return -1;
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

std::set<Point> getPinSet(const Net& net) {
    return std::set<Point>(net.pins.begin(), net.pins.end());
}

bool componentHasPin( const std::vector<Point>& component, const std::set<Point>& pins ) {
    for (const Point& p : component) {
        if (pins.count(p)) {
            return true;
        }
    }

    return false;
}

std::pair<Point, Point> makeEdgeKey(const Point& a, const Point& b) {
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

void removeNetFromEdge(RoutingDB& db, const Point& a, const Point& b, int netId) {
    Edge& edge = getEdge(db, getEdgeRefFromAdjPoints(a, b));
    edge.netIds.erase(netId);
    edge.usage = static_cast<int>(edge.netIds.size());
}

// 把 net 的繞線整理成一段一段的 chain：
// 每條 routedPath 都是從一個 key node（pin 或分岔點）走到下一個 key node，中間的點都剛好只有兩個鄰居。
// 重繞時新 path 可能接在別條 path 的中間，讓那條 path 中間多出分岔點；
// 如果不重新切開，之後拔掉那條 path 會讓 net 分成三塊以上，而 rip-up 只會接回兩塊。
// 切成 chain 之後，拔掉任何一條 path，剩下的 graph 最多只會分成兩塊。
// 順便移除不含 pin 的 component，以及不是 pin 的葉節點（多餘的線）。
void normalizeNetPaths(RoutingDB& db, int netId) {
    Net& net = db.nets[netId];
    const std::set<Point> pins = getPinSet(net);

    std::map<Point, std::set<Point>> adj;
    for (const auto& path : net.routedPaths) {
        for (int i = 0; i + 1 < static_cast<int>(path.size()); i++) {
            adj[path[i]].insert(path[i + 1]);
            adj[path[i + 1]].insert(path[i]);
        }
    }

    // 1. 移除不含 pin 的 component
    std::set<Point> visited;
    std::vector<Point> nodesToErase;
    for (const auto& item : adj) {
        if (visited.count(item.first)) {
            continue;
        }

        std::vector<Point> component;
        std::queue<Point> q;
        visited.insert(item.first);
        q.push(item.first);

        while (!q.empty()) {
            Point u = q.front();
            q.pop();
            component.push_back(u);

            for (const Point& v : adj[u]) {
                if (!visited.count(v)) {
                    visited.insert(v);
                    q.push(v);
                }
            }
        }

        if (!componentHasPin(component, pins)) {
            nodesToErase.insert(nodesToErase.end(), component.begin(), component.end());
        }
    }

    for (const Point& u : nodesToErase) {
        for (const Point& v : adj[u]) {
            if (u < v) {
                removeNetFromEdge(db, u, v, netId);
            }
        }
    }
    for (const Point& u : nodesToErase) {
        adj.erase(u);
    }

    // 2. 反覆剪掉不是 pin 的葉節點
    std::vector<Point> leaves;
    for (const auto& item : adj) {
        if (item.second.size() <= 1 && !pins.count(item.first)) {
            leaves.push_back(item.first);
        }
    }

    while (!leaves.empty()) {
        Point u = leaves.back();
        leaves.pop_back();

        auto it = adj.find(u);
        if (it == adj.end() || it->second.size() > 1) {
            continue;
        }

        for (const Point& v : it->second) {
            removeNetFromEdge(db, u, v, netId);
            adj[v].erase(u);

            if (adj[v].size() <= 1 && !pins.count(v)) {
                leaves.push_back(v);
            }
        }

        adj.erase(it);
    }

    // 3. 從每個 key node 出發，沿著只有兩個鄰居的點走到下一個 key node，得到一條 chain
    auto isKeyNode = [&](const Point& p) {
        return pins.count(p) > 0 || adj.at(p).size() != 2;
    };

    std::set<std::pair<Point, Point>> usedEdges;
    std::vector<std::vector<Point>> chains;

    for (const auto& item : adj) {
        if (!isKeyNode(item.first)) {
            continue;
        }

        for (const Point& next : item.second) {
            if (usedEdges.count(makeEdgeKey(item.first, next))) {
                continue;
            }

            std::vector<Point> chain = {item.first, next};
            usedEdges.insert(makeEdgeKey(item.first, next));

            Point prev = item.first;
            Point cur = next;

            while (!isKeyNode(cur)) {
                const std::set<Point>& neighbors = adj.at(cur);
                Point following = (*neighbors.begin() == prev) ? *neighbors.rbegin() : *neighbors.begin();

                usedEdges.insert(makeEdgeKey(cur, following));
                chain.push_back(following);

                prev = cur;
                cur = following;
            }

            chains.push_back(chain);
        }
    }

    // 不含 key node 的環一定不含 pin，已經在步驟 1 移除，所以每條邊都會被走到
    net.routedPaths = chains;
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
// 軟限制：會 overflow 的 edge 也可以走，但要付 present cost（超出越多越貴，而且每輪變貴）；
// 曾經 overflow 過的 edge 再乘上 history cost。
// 這樣 net 之間可以逐輪「協商」：真的沒有別條路的 net 留下，有其他選擇的 net 會被推走。
double getEdgeCost(const RoutingDB& db, const Point& a, const Point& b, int netId, double presentFactor) {
    EdgeRef ref = getEdgeRefFromAdjPoints(a, b);
    const Edge& e = getEdgeConst(db, ref);

    // 完全沒有 track 的 edge 仍然不能走
    if (e.capacity <= 0) {
        return INF_DOUBLE;
    }

    if (e.netIds.count(netId) > 0) {
        return 1.0;
    }

    const int overflowIfAdded = std::max(0, e.usage + 1 - e.capacity);
    const double presentCost = 1.0 + presentFactor * overflowIfAdded;
    const double historyCost = 1.0 + kHistoryWeight * e.historyCost;

    return historyCost * presentCost;
}

std::vector<Point> multiSourceMultiSinkDijkstra( const RoutingDB& db, const std::vector<Point>& sources, const std::vector<Point>& sinks, int netId, double presentFactor ) {
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
        State top = pq.top();
        double cost = top.first;
        Point u = top.second;
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

            double edgeCost = getEdgeCost(db, u, v, netId, presentFactor);

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

// 回傳這次成功重繞（或移除多餘 path）的次數
int OneRipupAndReroute(RoutingDB& db, const EdgeRef& overflowEdge, double presentFactor) {
    const Edge& edge = getEdgeConst(db, overflowEdge);

    // 先排序再 shuffle，結果才不會受 unordered_set 的走訪順序影響
    std::vector<int> candidates(edge.netIds.begin(), edge.netIds.end());
    std::sort(candidates.begin(), candidates.end());

    if (candidates.empty()) {
        return 0;
    }

    // random shuffle candidates to avoid deterministic behavior
    std::shuffle(candidates.begin(), candidates.end(), rng);

    int numRerouted = 0;

    for (int netId : candidates) {
        // 這條 edge 已經不 overflow 了，就不用再拆其他 net
        if (edge.usage <= edge.capacity) {
            break;
        }

        Net& net = db.nets[netId];

        int pathIdx = findPathUsingEdge(net, overflowEdge);
        if (pathIdx < 0) {
            continue;
        }

        std::vector<Point> oldPath = net.routedPaths[pathIdx];
        Point oldStart = oldPath.front();
        Point oldEnd = oldPath.back();

        // 1. rip up: 先移除舊 path
        net.routedPaths.erase(net.routedPaths.begin() + pathIdx);
        // 2. 更新 usage
        removePathFromEdges(db, oldPath, netId);
        // 3. 用剩下的 tree 建 graph
        auto graph = buildAdjList(net);
        // 4. 找被拆掉 path 兩端所在的 component
        //    每條 path 都是 key node 之間的 chain，所以剩下的 graph 最多只有這兩塊
        std::vector<Point> sourceComponent = getComponentNodes(graph, oldStart);
        std::vector<Point> sinkComponent = getComponentNodes(graph, oldEnd);

        const std::set<Point> pins = getPinSet(net);
        const bool needReconnect =
            !sameComponent(sourceComponent, oldEnd) &&
            componentHasPin(sourceComponent, pins) &&
            componentHasPin(sinkComponent, pins);

        if (needReconnect) {
            // 5. multi-source multi-sink Dijkstra
            std::vector<Point> newPath = multiSourceMultiSinkDijkstra( db, sourceComponent, sinkComponent, netId, presentFactor );

            // 6. 如果找不到路（被 capacity 為 0 的 edge 擋住），就還原
            if (newPath.empty()) {
                net.routedPaths.insert(net.routedPaths.begin() + pathIdx, oldPath);
                addPathToEdges(db, oldPath, netId);
                continue;
            }

            // 7. 成功: 加入新的 path 並更新 usage
            net.routedPaths.push_back(newPath);
            addPathToEdges(db, newPath, netId);
        }
        // 否則兩端仍然連通，或其中一端沒有 pin，代表這條 path 是多餘的，直接拿掉

        // 8. 新 path 可能接在別條 path 的中間，重新切成 chain
        normalizeNetPaths(db, netId);

        numRerouted++;
    }
    return numRerouted;
}

// 軟限制下某一輪可能讓結果變差，所以要記住目前最好的解
struct RoutingSnapshot {
    std::vector<std::vector<Edge>> hEdges;
    std::vector<std::vector<Edge>> vEdges;
    std::vector<std::vector<std::vector<Point>>> routedPaths;
};

RoutingSnapshot saveRouting(const RoutingDB& db) {
    RoutingSnapshot snapshot;
    snapshot.hEdges = db.hEdges;
    snapshot.vEdges = db.vEdges;
    for (const Net& net : db.nets) {
        snapshot.routedPaths.push_back(net.routedPaths);
    }
    return snapshot;
}

void restoreRouting(RoutingDB& db, const RoutingSnapshot& snapshot) {
    db.hEdges = snapshot.hEdges;
    db.vEdges = snapshot.vEdges;
    for (int netId = 0; netId < static_cast<int>(db.nets.size()); netId++) {
        db.nets[netId].routedPaths = snapshot.routedPaths[netId];
    }
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

    // FLUTE 的 path 可能互相重疊或交叉，先整理成 chain，rip-up 才能保證只把 net 分成兩塊
    for (int netId = 0; netId < static_cast<int>(db.nets.size()); netId++) {
        normalizeNetPaths(db, netId);
    }
    overflow = computeTotalOverflow(db);

    RoutingSnapshot best = saveRouting(db);
    int bestOverflow = overflow;
    int bestWirelength = computeTotalWirelength(db);
    int stagnantIterations = 0;
    double presentFactor = kInitialPresentFactor;

    int iteration = 1;
    while (iteration <= maxIterations && overflow > 0) {
        // 會順便替每條 overflow edge 增加 history cost
        std::vector<EdgeRef> overflowEdges = findOverflowEdges(db);

        // 從 overflow 最大的 edge 開始處理
        std::stable_sort(overflowEdges.begin(), overflowEdges.end(),
            [&db](const EdgeRef& a, const EdgeRef& b) {
                const Edge& ea = getEdgeConst(db, a);
                const Edge& eb = getEdgeConst(db, b);
                return ea.usage - ea.capacity > eb.usage - eb.capacity;
            });

        // 每一輪處理所有 overflow edge
        int numRerouted = 0;
        for (const EdgeRef& target : overflowEdges) {
            numRerouted += OneRipupAndReroute(db, target, presentFactor);
        }

        overflow = computeTotalOverflow(db);
        const int wirelength = computeTotalWirelength(db);

        std::cout << "[Router] Iteration " << iteration
                  << ": overflow edges = " << overflowEdges.size()
                  << ", rerouted = " << numRerouted
                  << ", overflow = " << overflow
                  << ", wirelength = " << wirelength
                  << "\n";

        if (overflow < bestOverflow || (overflow == bestOverflow && wirelength < bestWirelength)) {
            best = saveRouting(db);
            bestOverflow = overflow;
            bestWirelength = wirelength;
            stagnantIterations = 0;
        } else {
            stagnantIterations++;
        }

        if (stagnantIterations >= kMaxStagnantIterations) {
            std::cout << "[Router] No improvement in " << kMaxStagnantIterations
                      << " iterations, stop.\n";
            break;
        }

        presentFactor = std::min(presentFactor * kPresentFactorGrowth, kMaxPresentFactor);
        iteration++;
    }

    if (overflow > bestOverflow || (overflow == bestOverflow && computeTotalWirelength(db) > bestWirelength)) {
        restoreRouting(db, best);
    }
    std::cout << "\n";

    for (int netId = 0; netId < static_cast<int>(db.nets.size()); netId++) {
        buildOrderedPointsForNet(db, netId);
    }


    std::cout << "[Router] Final overflow = "
              << computeTotalOverflow(db) << "\n";
    std::cout << "[Router] Final Wirelength = "
              << computeTotalWirelength(db) << "\n\n";
}
