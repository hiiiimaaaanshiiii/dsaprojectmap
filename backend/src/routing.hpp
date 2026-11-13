#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace navigator {

struct Coordinate {
  double latitude{};
  double longitude{};
};

struct Edge {
  int to{};
  double meters{};
  double seconds{};
  std::string road_name;
};

struct Node {
  Coordinate point;
  std::string name;
};

enum class Objective { Fastest, Shortest };
enum class Algorithm { AStar, Dijkstra };

struct Maneuver {
  std::string instruction;
  Coordinate point;
  double meters_to_next{};
};

struct Route {
  bool found{false};
  std::vector<int> node_path;
  std::vector<Maneuver> maneuvers;
  double meters{};
  double seconds{};
  int visited_nodes{};
  Algorithm algorithm{Algorithm::AStar};
  std::string error;
};

class RoadGraph {
 public:
  bool load_csv(const std::string& filename, std::string& error);
  [[nodiscard]] int nearest_node(const Coordinate& point) const;
  [[nodiscard]] const Node& node(int index) const { return nodes_.at(index); }
  [[nodiscard]] const std::vector<Edge>& edges(int index) const { return adjacency_.at(index); }
  [[nodiscard]] std::size_t node_count() const { return nodes_.size(); }
  [[nodiscard]] std::size_t edge_count() const { return edge_count_; }

 private:
  struct SpatialNode {
    int graph_index{};
    int left{-1};
    int right{-1};
    bool split_latitude{};
  };

  int build_spatial_index(std::vector<int>& indices, int first, int last, int depth);
  void nearest_from(int tree_node, const Coordinate& point, int& nearest, double& nearest_distance) const;

  std::vector<Node> nodes_;
  std::vector<std::vector<Edge>> adjacency_;
  std::vector<SpatialNode> spatial_nodes_;
  int spatial_root_{-1};
  std::size_t edge_count_{};
};

class Router {
 public:
  explicit Router(const RoadGraph& graph) : graph_(graph) {}
  [[nodiscard]] Route find_route(const Coordinate& origin, const Coordinate& destination,
                                 Objective objective, Algorithm algorithm = Algorithm::AStar) const;

 private:
  [[nodiscard]] double heuristic(int from, int to, Objective objective) const;
  [[nodiscard]] std::vector<Maneuver> make_maneuvers(const std::vector<int>& path) const;
  const RoadGraph& graph_;
};

double haversine_meters(const Coordinate& first, const Coordinate& second);

}  // namespace navigator
