#include "routing.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <csignal>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

using navigator::Coordinate;
using navigator::Algorithm;
using navigator::Objective;
using navigator::RoadGraph;
using navigator::Route;
using navigator::Router;

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string json_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += character;
    }
  }
  return escaped;
}

std::string url_decode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '+' ) {
      decoded += ' ';
    } else if (value[index] == '%' && index + 2 < value.size()) {
      unsigned int code{};
      const auto [pointer, error] = std::from_chars(value.data() + index + 1, value.data() + index + 3, code, 16);
      if (error == std::errc() && pointer == value.data() + index + 3) {
        decoded += static_cast<char>(code);
        index += 2;
      } else {
        decoded += value[index];
      }
    } else {
      decoded += value[index];
    }
  }
  return decoded;
}

std::map<std::string, std::string> parse_query(const std::string& raw_query) {
  std::map<std::string, std::string> parameters;
  std::stringstream query(raw_query);
  std::string pair;
  while (std::getline(query, pair, '&')) {
    const auto delimiter = pair.find('=');
    const std::string key = url_decode(pair.substr(0, delimiter));
    const std::string value = delimiter == std::string::npos ? "" : url_decode(pair.substr(delimiter + 1));
    parameters[key] = value;
  }
  return parameters;
}

std::optional<double> decimal(const std::map<std::string, std::string>& parameters, const std::string& key) {
  const auto found = parameters.find(key);
  if (found == parameters.end()) return std::nullopt;
  try {
    return std::stod(found->second);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool valid_coordinate(const Coordinate& coordinate) {
  return coordinate.latitude >= -90.0 && coordinate.latitude <= 90.0 && coordinate.longitude >= -180.0 &&
         coordinate.longitude <= 180.0;
}

std::string coordinate_json(const Coordinate& coordinate) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6) << '[' << coordinate.longitude << ',' << coordinate.latitude << ']';
  return output.str();
}

std::string route_json(const Route& route, const RoadGraph& graph, Objective objective) {
  if (!route.found) return "{\"error\":\"" + json_escape(route.error) + "\"}";
  std::ostringstream output;
  output << std::fixed << std::setprecision(1);
  output << "{\"route\":{\"objective\":\"" << (objective == Objective::Fastest ? "fastest" : "shortest")
         << "\",\"algorithm\":\"" << (route.algorithm == Algorithm::AStar ? "A*" : "Dijkstra")
         << "\",\"distanceMeters\":" << route.meters
         << ",\"durationSeconds\":" << route.seconds << ",\"visitedNodes\":" << route.visited_nodes
         << "},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[";
  for (std::size_t index = 0; index < route.node_path.size(); ++index) {
    if (index > 0) output << ',';
    output << coordinate_json(graph.node(route.node_path[index]).point);
  }
  output << "]},\"maneuvers\":[";
  for (std::size_t index = 0; index < route.maneuvers.size(); ++index) {
    if (index > 0) output << ',';
    const auto& maneuver = route.maneuvers[index];
    output << "{\"instruction\":\"" << json_escape(maneuver.instruction) << "\",\"coordinate\":"
           << coordinate_json(maneuver.point) << ",\"metersToNext\":" << maneuver.meters_to_next << '}';
  }
  output << "]}";
  return output.str();
}

std::string search_json(const RoadGraph& graph, const std::string& raw_query) {
  const std::string query = lowercase(raw_query);
  std::ostringstream output;
  output << "{\"results\":[";
  int result_count = 0;
  for (std::size_t index = 0; index < graph.node_count() && result_count < 8; ++index) {
    const auto& node = graph.node(static_cast<int>(index));
    if (!query.empty() && lowercase(node.name).find(query) == std::string::npos) continue;
    if (result_count++ > 0) output << ',';
    output << "{\"id\":" << index << ",\"name\":\"" << json_escape(node.name)
           << "\",\"latitude\":" << node.point.latitude << ",\"longitude\":" << node.point.longitude << '}';
  }
  output << "]}";
  return output.str();
}

std::string status_json(const RoadGraph& graph) {
  return "{\"status\":\"ok\",\"engine\":\"C++ A* routing\",\"graph\":{\"nodes\":" +
         std::to_string(graph.node_count()) + ",\"directedEdges\":" + std::to_string(graph.edge_count()) + "}}";
}

void send_response(int client, int status, const std::string& payload) {
  const char* status_text = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: application/json; charset=utf-8\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Cache-Control: no-store\r\n"
           << "Content-Length: " << payload.size() << "\r\n"
           << "Connection: close\r\n\r\n" << payload;
  const std::string data = response.str();
  const char* position = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
    const auto sent = send(client, position, remaining, 0);
    if (sent <= 0) break;
    position += sent;
    remaining -= static_cast<std::size_t>(sent);
  }
}

std::string locate_graph() {
  if (const char* custom_path = std::getenv("NAVIGATOR_GRAPH")) return custom_path;
  const std::vector<std::string> candidates = {
      "backend/data/delhi_ncr_graph.csv", "data/delhi_ncr_graph.csv", "../backend/data/delhi_ncr_graph.csv",
      "backend/data/delhi_demo_graph.csv", "data/delhi_demo_graph.csv", "../backend/data/delhi_demo_graph.csv"};
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) return candidate;
  }
  return candidates.front();
}

void handle_request(int client, const RoadGraph& graph, const Router& router) {
  char request_buffer[16384];
  const auto bytes_read = recv(client, request_buffer, sizeof(request_buffer) - 1, 0);
  if (bytes_read <= 0) return;
  request_buffer[bytes_read] = '\0';
  const std::string request(request_buffer);
  const auto line_end = request.find("\r\n");
  const std::string request_line = request.substr(0, line_end);
  if (!request_line.starts_with("GET ")) {
    send_response(client, 400, "{\"error\":\"Only GET requests are supported\"}");
    return;
  }
  const auto target_end = request_line.find(' ', 4);
  if (target_end == std::string::npos) {
    send_response(client, 400, "{\"error\":\"Malformed request\"}");
    return;
  }
  const std::string target = request_line.substr(4, target_end - 4);
  const auto query_position = target.find('?');
  const std::string path = target.substr(0, query_position);
  const auto parameters = parse_query(query_position == std::string::npos ? "" : target.substr(query_position + 1));

  if (path == "/api/status") {
    send_response(client, 200, status_json(graph));
    return;
  }
  if (path == "/api/search") {
    const auto query = parameters.find("q");
    send_response(client, 200, search_json(graph, query == parameters.end() ? "" : query->second));
    return;
  }
  if (path == "/api/route") {
    const auto from_latitude = decimal(parameters, "fromLat");
    const auto from_longitude = decimal(parameters, "fromLng");
    const auto to_latitude = decimal(parameters, "toLat");
    const auto to_longitude = decimal(parameters, "toLng");
    if (!from_latitude || !from_longitude || !to_latitude || !to_longitude) {
      send_response(client, 400, "{\"error\":\"Origin and destination coordinates are required\"}");
      return;
    }
    const Coordinate origin{*from_latitude, *from_longitude};
    const Coordinate destination{*to_latitude, *to_longitude};
    if (!valid_coordinate(origin) || !valid_coordinate(destination)) {
      send_response(client, 400, "{\"error\":\"Coordinates are outside their valid ranges\"}");
      return;
    }
    const bool fastest = parameters.find("objective") == parameters.end() || parameters.at("objective") != "shortest";
    const Objective objective = fastest ? Objective::Fastest : Objective::Shortest;
    const Algorithm algorithm = parameters.find("algorithm") != parameters.end() && parameters.at("algorithm") == "dijkstra"
                                    ? Algorithm::Dijkstra
                                    : Algorithm::AStar;
    const Route route = router.find_route(origin, destination, objective, algorithm);
    send_response(client, route.found ? 200 : 400, route_json(route, graph, objective));
    return;
  }
  send_response(client, 404, "{\"error\":\"Endpoint not found\"}");
}

}  // namespace

int main(int argument_count, char* arguments[]) {
  std::signal(SIGPIPE, SIG_IGN);

  // Render supplies PORT; use 8080 when running locally.
  const char* port_env = std::getenv("PORT");
  const int port = port_env ? std::atoi(port_env) : 8080;
  if (port <= 0 || port > 65535) {
    std::cerr << "Invalid PORT environment variable\\n";
    return 1;
  }
  RoadGraph graph;
  std::string graph_error;
  const std::string graph_path = locate_graph();
  if (!graph.load_csv(graph_path, graph_error)) {
    std::cerr << "Unable to load routing graph: " << graph_error << '\n';
    return 1;
  }
  Router router(graph);
  if (argument_count == 2 && std::string(arguments[1]) == "--verify") {
    const Route route = router.find_route({28.6129, 77.2295}, {28.5702, 77.3240}, Objective::Fastest);
    if (!route.found) {
      std::cerr << "Full-network verification failed: " << route.error << '\n';
      return 2;
    }
    std::cout << "Verified full network route: " << route.node_path.size() << " road nodes, "
              << std::llround(route.meters) << " m, " << std::llround(route.seconds) << " s, A* visited "
              << route.visited_nodes << " nodes\n";
    return 0;
  }
  const int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    std::perror("socket");
    return 1;
  }
  int reuse_address = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 16) < 0) {
    std::perror("bind/listen");
    close(server);
    return 1;
  }
  std::cout << "Map Navigator API listening on 0.0.0.0:" << static_cast<unsigned short>(port) << "\n"
            << "Loaded " << graph.node_count() << " nodes and " << graph.edge_count() << " directed edges from "
            << graph_path << "\n";
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) continue;
      std::perror("accept");
      break;
    }
    handle_request(client, graph, router);
    close(client);
  }
  close(server);
  return 0;
}
