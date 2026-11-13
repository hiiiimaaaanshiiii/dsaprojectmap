#include "routing.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <queue>
#include <sstream>

namespace navigator {
namespace {

constexpr double kMaxSupportedSpeedMetersPerSecond = 110.0 / 3.6;
constexpr double kMaximumSnapDistanceMeters = 3000.0;

std::vector<std::string> split(const std::string& value, char separator) {
  std::vector<std::string> parts;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, separator)) parts.push_back(item);
  return parts;
}

bool starts_with_comment_or_empty(const std::string& value) {
  return value.empty() || value.front() == '#';
}

const Edge* edge_between(const RoadGraph& graph, int from, int to) {
  const auto& edges = graph.edges(from);
  const auto found = std::find_if(edges.begin(), edges.end(), [to](const Edge& edge) {
    return edge.to == to;
  });
  return found == edges.end() ? nullptr : &*found;
}

double bearing_degrees(const Coordinate& from, const Coordinate& to) {
  const double latitude1 = from.latitude * std::numbers::pi / 180.0;
  const double latitude2 = to.latitude * std::numbers::pi / 180.0;
  const double longitude_delta = (to.longitude - from.longitude) * std::numbers::pi / 180.0;
  const double y = std::sin(longitude_delta) * std::cos(latitude2);
  const double x = std::cos(latitude1) * std::sin(latitude2) -
                   std::sin(latitude1) * std::cos(latitude2) * std::cos(longitude_delta);
  return std::fmod(std::atan2(y, x) * 180.0 / std::numbers::pi + 360.0, 360.0);
}

std::string heading(double bearing) {
  static const std::vector<std::string> labels = {"north", "north-east", "east", "south-east",
                                                   "south", "south-west", "west", "north-west"};
  return labels.at(static_cast<std::size_t>(std::floor((bearing + 22.5) / 45.0)) % labels.size());
}

std::string turn_description(double previous_bearing, double next_bearing) {
  const double difference = std::fmod(next_bearing - previous_bearing + 540.0, 360.0) - 180.0;
  if (std::abs(difference) < 28.0) return "Continue";
  if (std::abs(difference) > 145.0) return "Make a U-turn";
  return difference > 0 ? "Turn right" : "Turn left";
}

}  // namespace

double haversine_meters(const Coordinate& first, const Coordinate& second) {
  constexpr double earth_radius_meters = 6371000.0;
  const double latitude_delta = (second.latitude - first.latitude) * std::numbers::pi / 180.0;
  const double longitude_delta = (second.longitude - first.longitude) * std::numbers::pi / 180.0;
  const double latitude1 = first.latitude * std::numbers::pi / 180.0;
  const double latitude2 = second.latitude * std::numbers::pi / 180.0;
  const double a = std::sin(latitude_delta / 2.0) * std::sin(latitude_delta / 2.0) +
                   std::cos(latitude1) * std::cos(latitude2) * std::sin(longitude_delta / 2.0) *
                       std::sin(longitude_delta / 2.0);
  return earth_radius_meters * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

bool RoadGraph::load_csv(const std::string& filename, std::string& error) {
  std::ifstream input(filename);
  if (!input) {
    error = "Unable to open graph file: " + filename;
    return false;
  }

  nodes_.clear();
  adjacency_.clear();
  spatial_nodes_.clear();
  spatial_root_ = -1;
  edge_count_ = 0;
  const auto add_edge_row = [&](const std::vector<std::string>& fields) -> bool {
    try {
      const int from = std::stoi(fields[1]);
      const int to = std::stoi(fields[2]);
      const double speed_kph = std::max(5.0, std::stod(fields[4]));
      const bool bidirectional = fields[5] != "F";
      if (from < 0 || to < 0 || static_cast<std::size_t>(from) >= nodes_.size() ||
          static_cast<std::size_t>(to) >= nodes_.size()) {
        error = "Edge references an unknown node";
        return false;
      }
      const double meters = haversine_meters(nodes_[from].point, nodes_[to].point);
      const auto add_directed_edge = [&](int origin, int destination) {
        adjacency_[origin].push_back(
            {destination, meters, meters / (speed_kph / 3.6), fields[3]});
        if (nodes_[origin].name.starts_with("OSM node ")) nodes_[origin].name = fields[3];
        if (nodes_[destination].name.starts_with("OSM node ")) nodes_[destination].name = fields[3];
        ++edge_count_;
      };
      add_directed_edge(from, to);
      if (bidirectional) add_directed_edge(to, from);
      return true;
    } catch (const std::exception&) {
      error = "Invalid edge row";
      return false;
    }
  };
  std::string line;
  while (std::getline(input, line)) {
    if (starts_with_comment_or_empty(line)) continue;
    const auto fields = split(line, '|');
    try {
      if (fields.size() >= 5 && fields[0] == "N") {
        const auto index = static_cast<std::size_t>(std::stoul(fields[1]));
        if (index != nodes_.size()) {
          error = "Node identifiers must be sequential in " + filename;
          return false;
        }
        nodes_.push_back({{std::stod(fields[2]), std::stod(fields[3])}, fields[4]});
        adjacency_.emplace_back();
      } else if (fields.size() >= 6 && fields[0] == "E") {
        if (!add_edge_row(fields)) return false;
      }
    } catch (const std::exception&) {
      error = "Invalid graph row: " + line;
      return false;
    }
  }

  if (nodes_.empty() || edge_count_ == 0) {
    error = "Graph has no navigable nodes or edges";
    return false;
  }
  std::vector<int> indices(nodes_.size());
  for (std::size_t index = 0; index < indices.size(); ++index) indices[index] = static_cast<int>(index);
  spatial_nodes_.reserve(nodes_.size());
  spatial_root_ = build_spatial_index(indices, 0, static_cast<int>(indices.size()), 0);
  return true;
}

int RoadGraph::nearest_node(const Coordinate& point) const {
  if (nodes_.empty() || spatial_root_ < 0) return -1;
  int nearest = -1;
  double nearest_distance = std::numeric_limits<double>::infinity();
  nearest_from(spatial_root_, point, nearest, nearest_distance);
  return nearest;
}

int RoadGraph::build_spatial_index(std::vector<int>& indices, int first, int last, int depth) {
  if (first >= last) return -1;
  const bool split_latitude = depth % 2 == 0;
  const int middle = first + (last - first) / 2;
  std::nth_element(indices.begin() + first, indices.begin() + middle, indices.begin() + last,
                   [&](int first_index, int second_index) {
                     const Coordinate& first_point = nodes_[first_index].point;
                     const Coordinate& second_point = nodes_[second_index].point;
                     return split_latitude ? first_point.latitude < second_point.latitude
                                           : first_point.longitude < second_point.longitude;
                   });
  const int position = static_cast<int>(spatial_nodes_.size());
  spatial_nodes_.push_back({indices[middle], -1, -1, split_latitude});
  spatial_nodes_[position].left = build_spatial_index(indices, first, middle, depth + 1);
  spatial_nodes_[position].right = build_spatial_index(indices, middle + 1, last, depth + 1);
  return position;
}

void RoadGraph::nearest_from(int tree_node, const Coordinate& point, int& nearest, double& nearest_distance) const {
  if (tree_node < 0) return;
  const SpatialNode& current = spatial_nodes_[tree_node];
  const Coordinate& current_point = nodes_[current.graph_index].point;
  const double distance = haversine_meters(point, current_point);
  if (distance < nearest_distance) {
    nearest_distance = distance;
    nearest = current.graph_index;
  }

  const double coordinate_delta = current.split_latitude ? point.latitude - current_point.latitude
                                                          : point.longitude - current_point.longitude;
  const int near_branch = coordinate_delta < 0.0 ? current.left : current.right;
  const int far_branch = coordinate_delta < 0.0 ? current.right : current.left;
  nearest_from(near_branch, point, nearest, nearest_distance);

  const double meters_per_degree = current.split_latitude
                                       ? 111132.0
                                       : 111320.0 * std::cos(point.latitude * std::numbers::pi / 180.0);
  if (std::abs(coordinate_delta) * meters_per_degree < nearest_distance) {
    nearest_from(far_branch, point, nearest, nearest_distance);
  }
}

double Router::heuristic(int from, int to, Objective objective) const {
  const double meters = haversine_meters(graph_.node(from).point, graph_.node(to).point);
  return objective == Objective::Shortest ? meters : meters / kMaxSupportedSpeedMetersPerSecond;
}

Route Router::find_route(const Coordinate& origin, const Coordinate& destination, Objective objective,
                         Algorithm algorithm) const {
  Route result;
  result.algorithm = algorithm;
  const int start = graph_.nearest_node(origin);
  const int goal = graph_.nearest_node(destination);
  if (start < 0 || goal < 0) {
    result.error = "The routing graph is unavailable";
    return result;
  }
  if (haversine_meters(origin, graph_.node(start).point) > kMaximumSnapDistanceMeters ||
      haversine_meters(destination, graph_.node(goal).point) > kMaximumSnapDistanceMeters) {
    result.error = "One or both points are outside the loaded Delhi NCR road coverage";
    return result;
  }

  const auto count = graph_.node_count();
  const double infinity = std::numeric_limits<double>::infinity();
  std::vector<double> best_cost(count, infinity);
  std::vector<int> parent(count, -1);
  using QueueEntry = std::pair<double, int>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> frontier;
  best_cost[start] = 0.0;
  const auto score_for = [&](int node) {
    return best_cost[node] + (algorithm == Algorithm::AStar ? heuristic(node, goal, objective) : 0.0);
  };
  frontier.push({score_for(start), start});

  while (!frontier.empty()) {
    const auto [score, current] = frontier.top();
    frontier.pop();
    const double expected_score = score_for(current);
    if (score > expected_score + 1e-6) continue;
    ++result.visited_nodes;
    if (current == goal) break;

    for (const Edge& edge : graph_.edges(current)) {
      const double weight = objective == Objective::Shortest ? edge.meters : edge.seconds;
      const double tentative_cost = best_cost[current] + weight;
      if (tentative_cost + 1e-6 < best_cost[edge.to]) {
        best_cost[edge.to] = tentative_cost;
        parent[edge.to] = current;
        frontier.push({score_for(edge.to), edge.to});
      }
    }
  }

  if (!std::isfinite(best_cost[goal])) {
    result.error = "No legal road route connects these points";
    return result;
  }

  for (int current = goal; current != -1; current = parent[current]) result.node_path.push_back(current);
  std::reverse(result.node_path.begin(), result.node_path.end());
  for (std::size_t index = 1; index < result.node_path.size(); ++index) {
    const Edge* edge = edge_between(graph_, result.node_path[index - 1], result.node_path[index]);
    if (edge == nullptr) {
      result.error = "Route reconstruction failed";
      return result;
    }
    result.meters += edge->meters;
    result.seconds += edge->seconds;
  }
  result.maneuvers = make_maneuvers(result.node_path);
  result.found = true;
  return result;
}

std::vector<Maneuver> Router::make_maneuvers(const std::vector<int>& path) const {
  std::vector<Maneuver> maneuvers;
  if (path.empty()) return maneuvers;
  if (path.size() == 1) {
    maneuvers.push_back({"You have arrived", graph_.node(path.front()).point, 0.0});
    return maneuvers;
  }

  const Edge* first_edge = edge_between(graph_, path[0], path[1]);
  const double first_bearing = bearing_degrees(graph_.node(path[0]).point, graph_.node(path[1]).point);
  maneuvers.push_back({"Head " + heading(first_bearing) + " on " + first_edge->road_name,
                       graph_.node(path[0]).point, first_edge->meters});

  for (std::size_t index = 1; index + 1 < path.size(); ++index) {
    const Edge* previous = edge_between(graph_, path[index - 1], path[index]);
    const Edge* next = edge_between(graph_, path[index], path[index + 1]);
    const double previous_bearing =
        bearing_degrees(graph_.node(path[index - 1]).point, graph_.node(path[index]).point);
    const double next_bearing =
        bearing_degrees(graph_.node(path[index]).point, graph_.node(path[index + 1]).point);
    if (previous->road_name != next->road_name || std::abs(previous_bearing - next_bearing) > 30.0) {
      maneuvers.push_back({turn_description(previous_bearing, next_bearing) + " onto " + next->road_name,
                           graph_.node(path[index]).point, next->meters});
    }
  }
  maneuvers.push_back({"Arrive at " + graph_.node(path.back()).name, graph_.node(path.back()).point, 0.0});
  return maneuvers;
}

}  // namespace navigator
