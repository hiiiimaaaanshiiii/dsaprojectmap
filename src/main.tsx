import { createRoot } from 'react-dom/client';
import * as maplibregl from 'maplibre-gl';
import workerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?worker&url';
import 'maplibre-gl/dist/maplibre-gl.css';
import './styles.css';
import { App } from './App';

// MapLibre v6 needs an explicit worker asset when bundled by Vite.
maplibregl.setWorkerUrl(workerUrl);

createRoot(document.getElementById('root')!).render(
  <App />,
);
