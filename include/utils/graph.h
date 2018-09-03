#pragma once
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
namespace rdc {
namespace graph {
template <class Node>
class UndirectedGraph {
public:
    UndirectedGraph() = default;
    ~UndirectedGraph() = default;
    UndirectedGraph(const std::vector<Node>& nodes,
            const std::vector<std::pair<Node, Node>>& edges) :
            nodes_(nodes), edges_(edges) {}
    UndirectedGraph(std::vector<Node>&& nodes,
            std::vector<std::pair<Node, Node>>&& edges) :
            nodes_(std::move(nodes)), edges_(std::move(edges)) {}
    UndirectedGraph(const UndirectedGraph& other) = default;
    void AddEdge(const Node& from, const Node& to) {
        edges_.emplace_back({from, to});
        adjacent_list_[from].emplace(to);
        adjacent_list_[to].emplace(from);
    }
    using DistanceDict = std::unordered_map<uint32_t,
        std::unordered_set<Node>>;
    DistanceDict ShortestDistances(const Node& from) {
        DistanceDict shortest_distances;
        std::unordered_map<Node, bool> visited;
        for (const auto& node : nodes_) {
            visited[node] = false;
        }
        std::queue<Node> cand;
        cand.emplace(from);
        uint32_t cur_distance = 1;
        shortest_distances[0] = from;
        while (!cand.empty) {
            const auto& cur_node = cand.front();
            cand.pop();
            for (const auto& adj_node: adjacent_list_[cur_node]) {
                if (!visited[adj_node]) {
                    shortest_distances[cur_distance].emplace(adj_node);
                    visited[adj_node] = true;
                    cand.emplace(adj_node);
                }
            }
            cur_distance += 1;
        }
        return shortest_distances;
    }
protected:
    void _BuildAdjacentList() {
        for (const auto& e : edges_) {
            const auto& from_node = e->first;
            const auto& to_node = e->second;
            auto& from_adjacents = adjacent_list_[from_node];
            if (!from_adjacents.count(to_node)) {
                from_adjacents.emplace(to_node);
            }
            auto& to_adjacents = adjacent_list_[to_node];
            if (!to_adjacents.count(from_node)) {
                to_adjacents.emplace(to_node);
            }
        }
    }
private:
    std::vector<Node> nodes_;
    std::vector<std::pair<Node, Node>> edges_;
    std::unordered_map<Node, std::unordered_set<Node>> adjacent_list_;
};
} // namespace graph
} // namespace rdc
