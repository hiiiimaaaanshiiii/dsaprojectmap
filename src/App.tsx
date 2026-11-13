import { useCallback, useEffect, useRef, useState } from 'react';
import * as maplibregl from 'maplibre-gl';
import type { GeoJSONSource, Map, MapMouseEvent, Marker } from 'maplibre-gl';
import {
  ArrowDownUp,
  ChevronRight,
  Clock3,
  Crosshair,
  Gauge,
  LocateFixed,
  MapPin,
  Navigation,
  Route,
  Search,
  ShieldCheck,
  TriangleAlert,
  X,
} from 'lucide-react';
import { getRoute, searchPlaces } from './api';
import type { Coordinate, Maneuver, Objective, Place, RouteResponse } from './types';

const INDIA_GATE: Coordinate = { latitude: 28.6129, longitude: 77.2295, label: 'India Gate' };
const CONNAUGHT_PLACE: Coordinate = { latitude: 28.6315, longitude: 77.2167, label: 'Connaught Place' };

const emptyRoute = {
  type: 'FeatureCollection' as const,
  features: [],
};

function asFeature(route: RouteResponse | null) {
  if (!route) return emptyRoute;
  return {
    type: 'FeatureCollection' as const,
    features: [{ type: 'Feature' as const, properties: {}, geometry: route.geometry }],
  };
}

function formatDistance(meters: number) {
  return meters >= 1000 ? `${(meters / 1000).toFixed(meters >= 10000 ? 0 : 1)} km` : `${Math.round(meters)} m`;
}

function formatDuration(seconds: number) {
  const minutes = Math.max(1, Math.round(seconds / 60));
  return minutes >= 60 ? `${Math.floor(minutes / 60)} hr ${minutes % 60} min` : `${minutes} min`;
}

function haversineMeters(first: Coordinate, second: Coordinate) {
  const radians = Math.PI / 180;
  const latitudeDelta = (second.latitude - first.latitude) * radians;
  const longitudeDelta = (second.longitude - first.longitude) * radians;
  const a = Math.sin(latitudeDelta / 2) ** 2 +
    Math.cos(first.latitude * radians) * Math.cos(second.latitude * radians) * Math.sin(longitudeDelta / 2) ** 2;
  return 6_371_000 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function distanceToRoute(point: Coordinate, route: RouteResponse | null) {
  if (!route?.geometry.coordinates.length) return Infinity;
  return Math.min(...route.geometry.coordinates.map(([longitude, latitude]) =>
    haversineMeters(point, { latitude, longitude, label: '' }),
  ));
}

type FieldName = 'origin' | 'destination';

function mapStyle() {
  return {
    version: 8 as const,
    sources: {
      osm: {
        type: 'raster' as const,
        tiles: ['https://tile.openstreetmap.org/{z}/{x}/{y}.png'],
        tileSize: 256,
        attribution: '© OpenStreetMap contributors',
      },
    },
    layers: [{ id: 'osm', type: 'raster' as const, source: 'osm' }],
  };
}

export function App() {
  const mapContainer = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<Map | null>(null);
  const originMarker = useRef<Marker | null>(null);
  const destinationMarker = useRef<Marker | null>(null);
  const userMarker = useRef<Marker | null>(null);
  const routeRef = useRef<RouteResponse | null>(null);
  const destinationRef = useRef<Coordinate>(CONNAUGHT_PLACE);
  const activeFieldRef = useRef<FieldName>('origin');
  const watchIdRef = useRef<number | null>(null);

  const [origin, setOrigin] = useState<Coordinate>(INDIA_GATE);
  const [destination, setDestination] = useState<Coordinate>(CONNAUGHT_PLACE);
  const [route, setRoute] = useState<RouteResponse | null>(null);
  const [objective, setObjective] = useState<Objective>('fastest');
  const [activeField, setActiveField] = useState<FieldName>('origin');
  const [searchText, setSearchText] = useState('');
  const [suggestions, setSuggestions] = useState<Place[]>([]);
  const [loadingRoute, setLoadingRoute] = useState(true);
  const [routeError, setRouteError] = useState('');
  const [isNavigating, setIsNavigating] = useState(false);
  const [liveLocation, setLiveLocation] = useState<Coordinate | null>(null);
  const [notice, setNotice] = useState('');

  useEffect(() => {
    activeFieldRef.current = activeField;
  }, [activeField]);

  useEffect(() => {
    routeRef.current = route;
  }, [route]);

  useEffect(() => {
    destinationRef.current = destination;
  }, [destination]);

  const putPoint = useCallback((field: FieldName, point: Coordinate) => {
    if (field === 'origin') setOrigin(point);
    else setDestination(point);
    setSearchText('');
    setSuggestions([]);
    setActiveField(field === 'origin' ? 'destination' : field);
  }, []);

  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;
    const map = new maplibregl.Map({
      container: mapContainer.current,
      style: mapStyle(),
      center: [77.2167, 28.6315],
      zoom: 11.2,
    });
    map.addControl(new maplibregl.NavigationControl({ showCompass: false }), 'bottom-right');
    map.addControl(new maplibregl.ScaleControl({ maxWidth: 90, unit: 'metric' }), 'bottom-left');
    map.on('load', () => {
      map.addSource('route', { type: 'geojson', data: emptyRoute });
      map.addLayer({
        id: 'route-outline', type: 'line', source: 'route',
        paint: { 'line-color': '#ffffff', 'line-width': 8, 'line-opacity': 0.9 },
      });
      map.addLayer({
        id: 'route-line', type: 'line', source: 'route',
        paint: { 'line-color': '#2479ff', 'line-width': 5, 'line-opacity': 1 },
      });
    });
    map.on('click', (event: MapMouseEvent) => {
      const field = activeFieldRef.current;
      putPoint(field, {
        latitude: event.lngLat.lat,
        longitude: event.lngLat.lng,
        label: field === 'origin' ? 'Pinned origin' : 'Pinned destination',
      });
    });
    mapRef.current = map;
    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, [putPoint]);

  useEffect(() => {
    const controller = new AbortController();
    const refreshRoute = async () => {
      setLoadingRoute(true);
      setRouteError('');
      try {
        const nextRoute = await getRoute(origin, destination, objective);
        if (!controller.signal.aborted) setRoute(nextRoute);
      } catch (error) {
        if (!controller.signal.aborted) {
          setRoute(null);
          setRouteError(error instanceof Error ? error.message : 'Unable to calculate a route.');
        }
      } finally {
        if (!controller.signal.aborted) setLoadingRoute(false);
      }
    };
    void refreshRoute();
    return () => controller.abort();
  }, [origin, destination, objective]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    const updateMap = () => {
      const source = map.getSource('route') as GeoJSONSource | undefined;
      source?.setData(asFeature(route));

      originMarker.current?.remove();
      destinationMarker.current?.remove();
      originMarker.current = new maplibregl.Marker({ color: '#101419', scale: 0.9 })
        .setLngLat([origin.longitude, origin.latitude]).setPopup(new maplibregl.Popup({ offset: 24 }).setText(origin.label)).addTo(map);
      destinationMarker.current = new maplibregl.Marker({ color: '#2479ff', scale: 0.9 })
        .setLngLat([destination.longitude, destination.latitude]).setPopup(new maplibregl.Popup({ offset: 24 }).setText(destination.label)).addTo(map);

      if (route?.geometry.coordinates.length && !isNavigating) {
        const bounds = route.geometry.coordinates.reduce(
          (result, coordinate) => result.extend(coordinate),
          new maplibregl.LngLatBounds(route.geometry.coordinates[0], route.geometry.coordinates[0]),
        );
        map.fitBounds(bounds, { padding: { top: 100, right: 460, bottom: 120, left: 80 }, maxZoom: 13, duration: 700 });
      }
    };
    if (map.isStyleLoaded()) updateMap();
    else map.once('load', updateMap);
  }, [destination, isNavigating, origin, route]);

  useEffect(() => {
    if (!liveLocation || !mapRef.current) return;
    const map = mapRef.current;
    userMarker.current?.remove();
    const element = document.createElement('div');
    element.className = 'live-location-dot';
    userMarker.current = new maplibregl.Marker({ element, rotationAlignment: 'map' })
      .setLngLat([liveLocation.longitude, liveLocation.latitude]).addTo(map);
    if (isNavigating) map.easeTo({ center: [liveLocation.longitude, liveLocation.latitude], zoom: Math.max(map.getZoom(), 14), duration: 700 });
  }, [isNavigating, liveLocation]);

  useEffect(() => () => {
    if (watchIdRef.current !== null) navigator.geolocation.clearWatch(watchIdRef.current);
  }, []);

  const updateSearch = async (value: string) => {
    setSearchText(value);
    try {
      const nextSuggestions = await searchPlaces(value);
      setSuggestions(nextSuggestions);
    } catch {
      setSuggestions([]);
    }
  };

  const switchPoints = () => {
    setOrigin(destination);
    setDestination(origin);
    setActiveField('origin');
  };

  const useCurrentLocation = () => {
    if (!navigator.geolocation) {
      setNotice('This browser does not support location services.');
      return;
    }
    setNotice('Finding your location…');
    navigator.geolocation.getCurrentPosition(
      ({ coords }) => {
        const point = { latitude: coords.latitude, longitude: coords.longitude, label: 'Current location' };
        setLiveLocation(point);
        putPoint('origin', point);
        mapRef.current?.flyTo({ center: [point.longitude, point.latitude], zoom: 14, essential: true });
        setNotice('Current location set as origin.');
      },
      () => setNotice('Location access was not granted. Choose a point on the map instead.'),
      { enableHighAccuracy: true, timeout: 10_000 },
    );
  };

  const stopNavigation = () => {
    if (watchIdRef.current !== null) navigator.geolocation.clearWatch(watchIdRef.current);
    watchIdRef.current = null;
    setIsNavigating(false);
    setNotice('Navigation paused.');
  };

  const startNavigation = () => {
    if (!navigator.geolocation) {
      setNotice('This browser does not support location services.');
      return;
    }
    setNotice('Navigation started. Keep this tab open to update your position.');
    setIsNavigating(true);
    watchIdRef.current = navigator.geolocation.watchPosition(
      ({ coords }) => {
        const point = { latitude: coords.latitude, longitude: coords.longitude, label: 'Current location' };
        setLiveLocation(point);
        if (distanceToRoute(point, routeRef.current) > 120) {
          setOrigin(point);
          setNotice('You are off route — finding a new path.');
        }
      },
      () => {
        setNotice('Location updates are unavailable. Navigation has been paused.');
        stopNavigation();
      },
      { enableHighAccuracy: true, maximumAge: 4_000, timeout: 12_000 },
    );
  };

  const currentManeuver: Maneuver | undefined = route?.maneuvers[0];

  return (
    <main className="app-shell">
      <div className="map" ref={mapContainer} aria-label="Interactive map of Delhi NCR" />
      <section className="planner-panel" aria-label="Route planner">
        <div className="brand-row">
          <div className="brand-mark"><Navigation size={18} strokeWidth={2.7} /></div>
          <div>
            <p className="eyebrow">C++ ROUTING ENGINE</p>
            <h1>Map Navigator</h1>
          </div>
          <span className="engine-badge"><ShieldCheck size={13} /> A*</span>
        </div>

        <div className="route-inputs">
          {(['origin', 'destination'] as const).map((field) => {
            const point = field === 'origin' ? origin : destination;
            const selected = activeField === field;
            return (
              <label className={`location-field ${selected ? 'selected' : ''}`} key={field}>
                <span className={`location-dot ${field}`} />
                <span className="field-content">
                  <span className="field-label">{field === 'origin' ? 'FROM' : 'TO'}</span>
                  <input
                    value={selected && searchText ? searchText : point.label}
                    onFocus={() => { setActiveField(field); setSearchText(''); void updateSearch(''); }}
                    onChange={(event) => void updateSearch(event.target.value)}
                    placeholder={field === 'origin' ? 'Choose an origin' : 'Choose a destination'}
                    aria-label={field === 'origin' ? 'Origin' : 'Destination'}
                  />
                </span>
                {selected && searchText && <button className="clear-search" onClick={() => { setSearchText(''); setSuggestions([]); }} aria-label="Clear search"><X size={15} /></button>}
              </label>
            );
          })}
          <button className="swap-button" onClick={switchPoints} aria-label="Swap origin and destination"><ArrowDownUp size={15} /></button>
          {activeField === 'origin' && suggestions.length > 0 && (
            <div className="suggestions">
              {suggestions.map((place) => (
                <button key={place.id} onMouseDown={(event) => event.preventDefault()} onClick={() => putPoint('origin', place)}>
                  <MapPin size={16} /><span>{place.label}</span><ChevronRight size={15} />
                </button>
              ))}
            </div>
          )}
          {activeField === 'destination' && suggestions.length > 0 && (
            <div className="suggestions">
              {suggestions.map((place) => (
                <button key={place.id} onMouseDown={(event) => event.preventDefault()} onClick={() => putPoint('destination', place)}>
                  <MapPin size={16} /><span>{place.label}</span><ChevronRight size={15} />
                </button>
              ))}
            </div>
          )}
        </div>

        <div className="quick-actions">
          <button onClick={useCurrentLocation}><LocateFixed size={15} /> Use current location</button>
          <span><Crosshair size={14} /> Click map to set {activeField}</span>
        </div>

        <div className="objective-switch" role="group" aria-label="Route objective">
          <button className={objective === 'fastest' ? 'active' : ''} onClick={() => setObjective('fastest')}><Gauge size={15} /> Fastest</button>
          <button className={objective === 'shortest' ? 'active' : ''} onClick={() => setObjective('shortest')}><Route size={15} /> Shortest</button>
        </div>

        {routeError ? (
          <div className="error-card"><TriangleAlert size={18} /><span>{routeError}</span></div>
        ) : (
          <div className="route-card">
            <div className="route-card-head"><span>{loadingRoute ? 'Calculating route…' : objective === 'fastest' ? 'Recommended route' : 'Shortest route'}</span><span className="live-pill">LIVE</span></div>
            <div className="route-stats">
              <div><Clock3 size={20} /><strong>{route ? formatDuration(route.route.durationSeconds) : '—'}</strong><small>estimated</small></div>
              <div><Route size={20} /><strong>{route ? formatDistance(route.route.distanceMeters) : '—'}</strong><small>distance</small></div>
            </div>
            {route && <p className="algorithm-note"><Search size={13} /> A* explored {route.route.visitedNodes} road nodes</p>}
          </div>
        )}

        <button className={`navigate-button ${isNavigating ? 'navigating' : ''}`} disabled={!route || loadingRoute} onClick={isNavigating ? stopNavigation : startNavigation}>
          <Navigation size={18} fill="currentColor" /> {isNavigating ? 'Pause navigation' : 'Start navigation'}
        </button>
        {notice && <p className="notice">{notice}</p>}
      </section>

      {isNavigating && currentManeuver && (
        <section className="navigation-banner">
          <div className="maneuver-icon"><Navigation size={20} fill="currentColor" /></div>
          <div><p>UP NEXT</p><strong>{currentManeuver.instruction}</strong></div>
          <span>{formatDistance(currentManeuver.metersToNext)}</span>
        </section>
      )}

      <footer className="map-footer">Routing on a custom C++ directed road graph · Map data © OpenStreetMap contributors</footer>
    </main>
  );
}
