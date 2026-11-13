# Map Navigator

An interactive Delhi NCR route-planning web app with a C++20 routing engine. The map is not a static illustration: the browser requests a route from the local C++ service, receives a road-following GeoJSON line and maneuvers, then draws it on the map. Navigation mode watches browser location (with permission), follows the position marker and asks the engine for a replacement route after a meaningful deviation.

## What is implemented

- React + TypeScript + MapLibre user interface with map click, place selection, route objectives, route geometry, distance, ETA and maneuver display.
- C++20 HTTP API with a directed adjacency-list road graph, OSM-ID-ready import format, A* and Dijkstra, binary min-heap frontier, Haversine heuristic, parent-vector route reconstruction and generated turn instructions.
- `fastest` (edge travel-time) and `shortest` (edge-distance) route objectives.
- A lightweight demo graph is included for quick setup. The full Delhi NCR graph is intentionally kept outside the Git repository because the source extract and generated graph are large local assets.
- An OSM XML importer that converts a permitted bounded extract into the same production graph format. It retains drivable roads and one-way rules.

## Run locally

The simplest way to start both the C++ API and the web interface is:

```bash
npm run dev:full
```

Then open `http://127.0.0.1:5173`.

Alternatively, open two terminals in this project directory.

```bash
make -C backend
./backend/bin/navigator_api
```

```bash
npm run dev
```

The frontend proxies `/api` calls to the C++ service on port `8080`. If the UI says the routing engine is unavailable, that second command is not running or has been stopped.

## Test the routing API

```bash
curl 'http://127.0.0.1:8080/api/status'
curl 'http://127.0.0.1:8080/api/search?q=India'
curl 'http://127.0.0.1:8080/api/route?fromLat=28.6129&fromLng=77.2295&toLat=28.5702&toLng=77.3240&objective=fastest'
curl 'http://127.0.0.1:8080/api/route?fromLat=28.6129&fromLng=77.2295&toLat=28.5702&toLng=77.3240&objective=fastest&algorithm=dijkstra'
```

The final two commands should return the same optimal distance/duration. A* should normally visit fewer nodes; the frontend intentionally uses A*.

## Graph format and fuller OSM coverage

The server loads `backend/data/delhi_ncr_graph.csv` when present, then falls back to the small `delhi_demo_graph.csv`. The full graph is not bundled in this lightweight repository upload. The format is deliberately simple:

```text
N|internal-id|latitude|longitude|display name
E|from-id|to-id|road name|speed-kph|B-or-F
```

`B` produces two directed edges; `F` preserves a one-way edge. For a larger dataset, obtain a bounded OSM **XML** extract from a source whose terms allow it, then run:

```bash
python3 backend/tools/osm_to_graph.py /path/to/delhi.osm backend/data/delhi_graph.csv
NAVIGATOR_GRAPH=backend/data/delhi_graph.csv ./backend/bin/navigator_api
```

The importer purposefully does not bulk-download data from public OSM services. The bundled full graph is a one-time, bounded import. The API uses an in-memory KD-tree for nearest-road snapping. Before calling the large-city version production-ready, add turn-restriction relations and a production tile/geocoding provider.

## Map and provider notes

MapLibre renders the map; it is not the routing provider. The bundled demo uses OpenStreetMap raster tiles only for normal interactive development. Preserve visible attribution and do not bulk-prefetch or scrape those public tiles. Public Nominatim should not be used for client-side autocomplete; replace the local landmark search with a properly configured geocoding-provider adapter for broader place search.

## Project layout

```text
src/                    React map and navigation UI
backend/src/            C++ graph, algorithms and HTTP API
backend/data/           runnable Delhi NCR starter graph
backend/tools/          OSM XML to graph converter
```
