#include "LayerAssignment.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// 輸入檔沒有給 pin 在哪一層，這裡假設 pin 都在最底層 (M1)，
// 所以 pin 所在的 Gcell 也要算上從 M1 疊上去的 via
constexpr int kPinLayerIdx = 0;

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

bool layerMatchesDirection(const RoutingDB& db, int layerIdx, bool horizontal) {
    const std::string& dir = db.layers[layerIdx].preferredDirection;
    return horizontal ? dir == "horizontal" : dir == "vertical";
}

// 依序比較：先比 overflow，再比 via 數，最後比擁擠程度
struct LayerCost {
    int overflow;
    int vias;
    double congestion;

    LayerCost(int overflow = 0, int vias = 0, double congestion = 0.0)
        : overflow(overflow), vias(vias), congestion(congestion) {}

    LayerCost operator+(const LayerCost& other) const {
        return LayerCost(overflow + other.overflow, vias + other.vias, congestion + other.congestion);
    }

    bool operator<(const LayerCost& other) const {
        if (overflow != other.overflow) return overflow < other.overflow;
        if (vias != other.vias) return vias < other.vias;
        return congestion < other.congestion;
    }
};

// 把這個 net 放到 edge 的 layerIdx 上要付出的代價
LayerCost edgeLayerCost(const Edge& edge, int layerIdx) {
    const int capacity = edge.layerCapacity[layerIdx];
    const int nextUsage = edge.layerUsage[layerIdx] + 1;

    LayerCost cost;
    cost.overflow = (nextUsage > capacity) ? 1 : 0;
    cost.congestion = (capacity > 0) ? static_cast<double>(nextUsage) / capacity : nextUsage;
    return cost;
}

// 一個 Gcell 上用到的最低層到最高層之間，每跨一層算一個 via
int viaCountAtNode(const std::set<int>& layersAtNode) {
    if (layersAtNode.empty()) {
        return 0;
    }
    return *layersAtNode.rbegin() - *layersAtNode.begin();
}

// 以 net 的繞線建一棵 spanning tree：parent 一定排在 child 前面
struct NetTree {
    std::vector<Point> nodes;
    std::vector<int> parent;
    std::vector<std::vector<int>> children;
    std::vector<int> order;
    std::vector<int> roots;
    std::vector<std::pair<int, int>> extraEdges;   // 形成環、不在 tree 上的邊
};

NetTree buildNetTree(const Net& net, const std::map<Point, std::vector<Point>>& adjList) {
    NetTree tree;
    std::map<Point, int> indexOf;

    for (const auto& item : adjList) {
        indexOf[item.first] = static_cast<int>(tree.nodes.size());
        tree.nodes.push_back(item.first);
    }

    const int n = static_cast<int>(tree.nodes.size());
    tree.parent.assign(n, -1);
    tree.children.assign(n, std::vector<int>());

    // 優先從第一個 pin 開始；如果 net 不連通，其他 component 再各自當一棵樹
    std::vector<int> rootCandidates;
    auto pinIt = indexOf.find(Point(net.pins[0].x, net.pins[0].y));
    if (pinIt != indexOf.end()) {
        rootCandidates.push_back(pinIt->second);
    }
    for (int i = 0; i < n; i++) {
        rootCandidates.push_back(i);
    }

    std::vector<bool> visited(n, false);
    std::set<std::pair<int, int>> treeEdges;

    for (int root : rootCandidates) {
        if (visited[root]) {
            continue;
        }

        tree.roots.push_back(root);
        visited[root] = true;
        tree.order.push_back(root);

        std::vector<int> stack = {root};
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();

            for (const Point& p : adjList.at(tree.nodes[u])) {
                int v = indexOf.at(p);
                if (visited[v]) {
                    continue;
                }

                visited[v] = true;
                tree.parent[v] = u;
                tree.children[u].push_back(v);
                tree.order.push_back(v);
                treeEdges.insert(std::make_pair(std::min(u, v), std::max(u, v)));
                stack.push_back(v);
            }
        }
    }

    for (int u = 0; u < n; u++) {
        for (const Point& p : adjList.at(tree.nodes[u])) {
            int v = indexOf.at(p);
            if (u < v && !treeEdges.count(std::make_pair(u, v))) {
                tree.extraEdges.push_back(std::make_pair(u, v));
            }
        }
    }

    return tree;
}

struct NetAssignResult {
    int vias = 0;
    int overflow = 0;
};

// 對一個 net 做 tree DP，在不 overflow 的前提下讓 via 數最少。
// sub[v][l] = 「parent 到 v 的邊放在第 l 層」時，這條邊加上 v 底下整棵子樹的最小代價。
// 一個點的 via 數只跟它用到的最低層 lo 和最高層 hi 有關，
// 所以列舉 [lo, hi]，每個 child 各自在範圍內挑最好的層即可。
NetAssignResult assignLayersForNet(
    RoutingDB& db,
    Net& net,
    std::map<UnitEdgeRef, int>& edgeToLayer
) {
    NetAssignResult result;

    const auto adjList = buildAdjListUnique(net);
    if (adjList.empty()) {
        return result;
    }

    const NetTree tree = buildNetTree(net, adjList);
    if (tree.roots.size() > 1) {
        std::cerr << "[LayerAssignment Warning] Net " << net.name
                  << " is split into " << tree.roots.size() << " disconnected parts\n";
    }

    const int numNodes = static_cast<int>(tree.nodes.size());
    const int numLayers = static_cast<int>(db.layers.size());
    const std::set<Point> pins(net.pins.begin(), net.pins.end());

    std::vector<std::vector<LayerCost>> sub(numNodes, std::vector<LayerCost>(numLayers));
    std::vector<std::vector<bool>> subOk(numNodes, std::vector<bool>(numLayers, false));
    // bestRange[v][l + 1]：parent 邊在第 l 層時，v 選的 [lo, hi]；index 0 給 root（沒有 parent 邊）
    std::vector<std::vector<std::pair<int, int>>> bestRange(
        numNodes, std::vector<std::pair<int, int>>(numLayers + 1, std::make_pair(-1, -1))
    );

    // 在 parent 邊層數為 parentLayer（root 為 -1）時，求 v 的最小代價和選到的範圍
    auto solveNode = [&](int v, int parentLayer, LayerCost& bestCost) -> std::pair<int, int> {
        std::pair<int, int> best(-1, -1);

        for (int lo = 0; lo < numLayers; lo++) {
            for (int hi = lo; hi < numLayers; hi++) {
                if (parentLayer >= 0 && (parentLayer < lo || parentLayer > hi)) {
                    continue;
                }
                if (pins.count(tree.nodes[v]) && (kPinLayerIdx < lo || kPinLayerIdx > hi)) {
                    continue;
                }

                LayerCost total(0, hi - lo, 0.0);
                bool feasible = true;

                for (int c : tree.children[v]) {
                    int bestLayer = -1;
                    for (int l = lo; l <= hi; l++) {
                        if (subOk[c][l] && (bestLayer < 0 || sub[c][l] < sub[c][bestLayer])) {
                            bestLayer = l;
                        }
                    }

                    if (bestLayer < 0) {
                        feasible = false;
                        break;
                    }
                    total = total + sub[c][bestLayer];
                }

                if (feasible && (best.first < 0 || total < bestCost)) {
                    bestCost = total;
                    best = std::make_pair(lo, hi);
                }
            }
        }

        return best;
    };

    auto edgeBetween = [&](int u, int v) {
        return normalizeEdge(tree.nodes[u], tree.nodes[v]);
    };

    // bottom-up
    for (int i = numNodes - 1; i >= 0; i--) {
        const int v = tree.order[i];
        const int p = tree.parent[v];

        if (p < 0) {
            LayerCost rootCost;
            bestRange[v][0] = solveNode(v, -1, rootCost);
            if (bestRange[v][0].first < 0) {
                throw std::runtime_error("[LayerAssignment Error] No routing layer for some direction in net " + net.name);
            }
            continue;
        }

        const UnitEdgeRef edgeRef = edgeBetween(p, v);
        const Edge& edge = getEdgeFromUnitEdge(db, edgeRef);
        const bool horizontal = isHorizontalEdge(edgeRef);

        for (int l = 0; l < numLayers; l++) {
            if (!layerMatchesDirection(db, l, horizontal)) {
                continue;
            }

            LayerCost nodeCost;
            std::pair<int, int> range = solveNode(v, l, nodeCost);
            if (range.first < 0) {
                continue;
            }

            bestRange[v][l + 1] = range;
            sub[v][l] = edgeLayerCost(edge, l) + nodeCost;
            subOk[v][l] = true;
        }
    }

    // top-down：依照每個點選好的範圍，替每個 child 邊決定層
    std::vector<int> parentLayer(numNodes, -1);
    for (int v : tree.order) {
        const std::pair<int, int> range = bestRange[v][parentLayer[v] + 1];

        for (int c : tree.children[v]) {
            int bestLayer = -1;
            for (int l = range.first; l <= range.second; l++) {
                if (subOk[c][l] && (bestLayer < 0 || sub[c][l] < sub[c][bestLayer])) {
                    bestLayer = l;
                }
            }

            parentLayer[c] = bestLayer;
            edgeToLayer[edgeBetween(v, c)] = bestLayer;
        }
    }

    // 每個點目前用到的層
    std::vector<std::set<int>> layersAtNode(numNodes);
    for (int v = 0; v < numNodes; v++) {
        if (pins.count(tree.nodes[v])) {
            layersAtNode[v].insert(kPinLayerIdx);
        }
        if (tree.parent[v] >= 0) {
            layersAtNode[v].insert(parentLayer[v]);
            layersAtNode[tree.parent[v]].insert(parentLayer[v]);
        }
    }

    // 不在 tree 上的邊（繞線形成環）：挑新增 via 最少的層
    for (const auto& item : tree.extraEdges) {
        const int u = item.first;
        const int v = item.second;
        const UnitEdgeRef edgeRef = edgeBetween(u, v);
        const Edge& edge = getEdgeFromUnitEdge(db, edgeRef);
        const bool horizontal = isHorizontalEdge(edgeRef);

        int bestLayer = -1;
        LayerCost bestCost;

        for (int l = 0; l < numLayers; l++) {
            if (!layerMatchesDirection(db, l, horizontal)) {
                continue;
            }

            std::set<int> layersU = layersAtNode[u];
            std::set<int> layersV = layersAtNode[v];
            const int viasBefore = viaCountAtNode(layersU) + viaCountAtNode(layersV);
            layersU.insert(l);
            layersV.insert(l);
            const int addedVias = viaCountAtNode(layersU) + viaCountAtNode(layersV) - viasBefore;

            LayerCost cost = edgeLayerCost(edge, l) + LayerCost(0, addedVias, 0.0);
            if (bestLayer < 0 || cost < bestCost) {
                bestLayer = l;
                bestCost = cost;
            }
        }

        if (bestLayer < 0) {
            throw std::runtime_error("[LayerAssignment Error] No routing layer for some direction in net " + net.name);
        }

        edgeToLayer[edgeRef] = bestLayer;
        layersAtNode[u].insert(bestLayer);
        layersAtNode[v].insert(bestLayer);
    }

    // 更新每層的 usage；沒有空間時照樣放上去，記成 overflow，不中斷整個流程
    for (const auto& item : edgeToLayer) {
        Edge& edge = getEdgeFromUnitEdge(db, item.first);
        const int layerIdx = item.second;

        edge.layerUsage[layerIdx]++;
        if (edge.layerUsage[layerIdx] > edge.layerCapacity[layerIdx]) {
            result.overflow++;
        }
    }

    for (int v = 0; v < numNodes; v++) {
        result.vias += viaCountAtNode(layersAtNode[v]);
    }

    return result;
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

    // net 不連通時，其他 component 的線段也要輸出，不能漏掉
    for (const auto& item : adjList) {
        if (!visitedNodes.count(item.first)) {
            buildSegmentsDfs(item.first, adjList, visitedNodes, visitedEdges, edgeToLayer, db, net);
        }
    }
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
    std::cout << "[LayerAssignment] Start layer assignment...\n";

    resetLayerUsage(db);

    int totalVias = 0;
    int totalOverflow = 0;

    for (Net& net : db.nets) {
        std::map<UnitEdgeRef, int> edgeToLayer;
        NetAssignResult result = assignLayersForNet(db, net, edgeToLayer);
        buildAssignedSegmentsForNet(db, net, edgeToLayer);

        totalVias += result.vias;
        totalOverflow += result.overflow;
    }

    std::cout << "[LayerAssignment] Total vias = " << totalVias
              << " (pins assumed on " << db.layers[kPinLayerIdx].name << ")\n";
    std::cout << "[LayerAssignment] Per-layer overflow = " << totalOverflow << "\n";
    if (totalOverflow > 0) {
        std::cerr << "[LayerAssignment Warning] 2D routing still has overflow, "
                  << "some segments are placed on full layers\n";
    }
    std::cout << "[LayerAssignment] Finished layer assignment.\n\n";
}
