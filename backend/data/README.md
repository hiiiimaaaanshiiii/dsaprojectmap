# Map data

The repository intentionally keeps only `delhi_demo_graph.csv` so GitHub stays lightweight.

The full Delhi NCR source extract (`delhi_ncr.osm`) and generated production graph (`delhi_ncr_graph.csv`) are large local assets and are not included in this upload package. Keep your original copies outside Git, or distribute them separately (for example with Git LFS or a release asset) if the full dataset must be shared.

## Run with the included demo graph

From the repository root, build and start the backend:

```bash
make -C backend
./backend/bin/navigator_api
```

The backend automatically falls back to `delhi_demo_graph.csv` when the full graph is absent.

## Rebuild the full graph locally

If you have a permitted OSM XML extract, run:

```bash
python3 backend/tools/osm_to_graph.py /path/to/delhi_ncr.osm backend/data/delhi_ncr_graph.csv
NAVIGATOR_GRAPH=backend/data/delhi_ncr_graph.csv ./backend/bin/navigator_api
```
