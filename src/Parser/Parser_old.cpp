// Parser/Parser.cpp
#include "Parser.hpp"
#include "Structure.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace {

enum class Section {
    None,
    GCell,
    Nets
};

std::string stripComment(const std::string& line) {
    // Remove comments from the line. Comments start with '#' and continue to the end of the line.
    std::size_t pos = line.find('#');   // find(): returns the index of the first occurrence of '#' or std::string::npos if not found

    if (pos == std::string::npos) { // No comment found, return the original line
        return line;
    }

    return line.substr(0, pos); // substr(start, length): returns a substring starting at index 'start' with the specified 'length'. If 'length' is not provided, it goes to the end of the string.
}

std::string trim(const std::string& s) {
    std::size_t left = 0;
    std::size_t right = s.size();

    while (left < right && std::isspace(static_cast<unsigned char>(s[left]))) {
        left++;
    }

    while (right > left && std::isspace(static_cast<unsigned char>(s[right - 1]))) {
        right--;
    }

    return s.substr(left, right - left);
}

// 讓 T(M2 3) 也可以被切成 T ( M2 3 )，變成一個vector<string> {"T", "(", "M2", "3", ")"}
std::vector<std::string> tokenize(const std::string& line) {
    std::string normalized;

    for (char c : line) {
        if (c == '(' || c == ')') {
            normalized += ' ';
            normalized += c;
            normalized += ' ';
        } else {
            normalized += c;
        }
    }

    std::vector<std::string> tokens;
    std::stringstream ss(normalized);
    std::string token;

    while (ss >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

bool inGrid(const RoutingDB& db, int x, int y) {
    return x >= 0 && x < db.cols && y >= 0 && y < db.rows;
}

int collapseCapacityByDirection(
    const RoutingDB& db,
    char dir,
    const std::map<std::string, int>& layerCaps
) {
    int cap = 0;

    bool isHorizontalEdge = (dir == 'L' || dir == 'R');
    bool isVerticalEdge = (dir == 'B' || dir == 'T');

    for (const auto& item : layerCaps) {
        const std::string& layerName = item.first;
        int layerCap = item.second;

        auto it = db.layerPref.find(layerName);

        if (it == db.layerPref.end()) {
            std::cerr << "[Warning] unknown layer: " << layerName << "\n";
            continue;
        }

        const std::string& preferredDirection = it->second;

        if (isHorizontalEdge && preferredDirection == "horizontal") {
            cap += layerCap;
        }

        if (isVerticalEdge && preferredDirection == "vertical") {
            cap += layerCap;
        }
    }

    return cap;
}

void setEdgeCapacity(Edge& edge, int cap) {
    // If the edge is not initialized, set its capacity directly.
    if (!edge.initialized) {
        edge.capacity = cap;
        edge.initialized = true;
    } else {
        if (edge.capacity != cap) {
            std::cerr << "[Warning] inconsistent edge capacity: "
                      << edge.capacity << " vs " << cap
                      << ". Use min value.\n";

            edge.capacity = std::min(edge.capacity, cap);
        }
    }
}

void assignDirectionalCapacity(
    RoutingDB& db,
    int x,
    int y,
    char dir,
    const std::map<std::string, int>& layerCaps
) {
    int cap = collapseCapacityByDirection(db, dir, layerCaps);

    if (dir == 'R') {
        if (x + 1 < db.cols) {
            setEdgeCapacity(db.hEdges[x][y], cap);
        }
    }
    else if (dir == 'L') {
        if (x - 1 >= 0) {
            setEdgeCapacity(db.hEdges[x - 1][y], cap);
        }
    }
    else if (dir == 'T') {
        if (y + 1 < db.rows) {
            setEdgeCapacity(db.vEdges[x][y], cap);
        }
    }
    else if (dir == 'B') {
        if (y - 1 >= 0) {
            setEdgeCapacity(db.vEdges[x][y - 1], cap);
        }
    }
    else {
        throw std::runtime_error("Unknown direction in GC line");
    }
}

void parseRoutingLayers(RoutingDB& db, const std::vector<std::string>& tok) {
    // Example: routing_layers 3 M1 horizontal M2 vertical M3 horizontal
    if (tok.size() < 2) {
        throw std::runtime_error("[Error] Invalid routing_layers line");
    }

    int numLayers = std::stoi(tok[1]);
    std::size_t idx = 2;    // Start with the first layer definition

    db.layers.clear();
    db.layerPref.clear();

    // Each layer is defined by a pair of tokens: layer name and preferred direction
    for (int i = 0; i < numLayers; i++) {
        if (idx + 1 >= tok.size()) {
            throw std::runtime_error("[Error] Not enough tokens in routing_layers line");
        }

        Layer layer;
        layer.name = tok[idx++];
        layer.preferredDirection = tok[idx++];

        db.layers.push_back(layer);
        db.layerPref[layer.name] = layer.preferredDirection;
    }
}

void parseGCellGrid(RoutingDB& db, const std::vector<std::string>& tok) {
    // Example: Gcell_grid 5 4
    if (tok.size() < 3) {
        throw std::runtime_error("[Error] Invalid Gcell_grid line");
    }

    db.cols = std::stoi(tok[1]);
    db.rows = std::stoi(tok[2]);

    if (db.cols <= 0 || db.rows <= 0) {
        throw std::runtime_error("[Error] Grid size must be positive");
    }

    // hEdges[x][y] = edge between (x, y) and (x+1, y)
    db.hEdges.assign(db.cols - 1, std::vector<Edge>(db.rows));

    // vEdges[x][y] = edge between (x, y) and (x, y+1)
    db.vEdges.assign(db.cols, std::vector<Edge>(db.rows - 1));
}

void parseGCellLine(RoutingDB& db, const std::vector<std::string>& tok) {
    // Example: GC 0 0  L ( M1 0  M3 0 )  R ( M1 1  M3 3 )  B ( M2 0 )  T ( M2 3 )
    if (tok.size() < 4 || tok[0] != "GC") {
        throw std::runtime_error("[Error] Invalid GC line");
    }

    int x = std::stoi(tok[1]);
    int y = std::stoi(tok[2]);

    if (!inGrid(db, x, y)) {
        throw std::runtime_error("[Error] GC coordinate out of range");
    }

    std::size_t i = 3;  // Start parsing from the 4th token

    while (i < tok.size()) {
        char dir = tok[i][0];   // Expecting a single character for direction (L/R/B/T)

        if (dir != 'L' && dir != 'R' && dir != 'B' && dir != 'T') {
            throw std::runtime_error("[Error] Expected direction L/R/B/T in GC line");
        }

        i++;

        if (i >= tok.size() || tok[i] != "(") {
            throw std::runtime_error("[Error] Expected '(' after direction in GC line");
        }

        i++;

        std::map<std::string, int> layerCaps;

        while (i < tok.size() && tok[i] != ")") {
            std::string layerName = tok[i++];

            if (i >= tok.size()) {
                throw std::runtime_error("[Error] Missing capacity after layer name");
            }

            int cap = std::stoi(tok[i++]);
            layerCaps[layerName] = cap;
        }

        if (i >= tok.size() || tok[i] != ")") {
            throw std::runtime_error("[Error] Expected ')' in GC line");
        }

        i++;

        assignDirectionalCapacity(db, x, y, dir, layerCaps);
    }
}

void parseNetLine(RoutingDB& db, const std::vector<std::string>& tok, int netId) {
    // Example: N1 ( 0 0 ) ( 1 0 ) ( 2 2 )
    if (tok.empty()) {
        return;
    }

    Net net;
    net.name = tok[0];

    std::size_t i = 1;

    while (i < tok.size()) {
        if (tok[i] != "(") {
            i++;
            continue;
        }

        if (i + 3 >= tok.size()) {
            throw std::runtime_error("[Error] Invalid pin format in net: " + net.name);
        }

        int x = std::stoi(tok[i + 1]);
        int y = std::stoi(tok[i + 2]);

        if (tok[i + 3] != ")") {
            throw std::runtime_error("[Error] Expected ')' after pin coordinate in net: " + net.name);
        }

        if (!inGrid(db, x, y)) {
            throw std::runtime_error("[Error] Pin coordinate out of range in net: " + net.name);
        }

        net.pins.emplace_back(x, y);
        i += 4;
    }

    if (net.pins.empty()) {
        throw std::runtime_error("[Error] Net has no pins: " + net.name);
    }

    net.id = netId;

    db.nets.push_back(net);
}

} // namespace

RoutingDB parseInputFile(const std::string& filename) {
    std::ifstream fin(filename);
    std::cout << "[Parser] Start parsing...\n";

    if (!fin) {
        throw std::runtime_error("[Error] Cannot open input file: " + filename);
    }

    RoutingDB db;
    Section section = Section::None;
    std::string line;
    int netId = 0;

    while (std::getline(fin, line)) {
        line = stripComment(line);
        line = trim(line);

        if (line.empty()) {
            continue;
        }

        std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) {
            continue;
        }

        if (tok[0] == "routing_layers") {
            parseRoutingLayers(db, tok);
            continue;
        }

        if (tok[0] == "Gcell_grid") {
            parseGCellGrid(db, tok);
            continue;
        }

        if (tok[0] == "Gcell") {
            section = Section::GCell;
            continue;
        }

        if (tok[0] == "end_of_Gcell") {
            section = Section::None;
            continue;
        }

        if (tok[0] == "nets") {
            section = Section::Nets;
            continue;
        }

        if (tok[0] == "end_of_nets") {
            section = Section::None;
            continue;
        }

        if (section == Section::GCell) {
            parseGCellLine(db, tok);
        } else if (section == Section::Nets) {
            parseNetLine(db, tok, netId);
            netId++;
        }
    }

    std::cout << "[Parser] Parsing completed.\n";
    std::cout << "Grid: " << db.cols << " x " << db.rows << "\n";
    std::cout << "Layers: " << db.layers.size() << "\n";

    for (const auto& layer : db.layers) {
        std::cout << "  " << layer.name << " (preferred direction: " << layer.preferredDirection << ")\n";
    }

    std::cout << "Nets: " << db.nets.size() << "\n\n";

    return db;
}
