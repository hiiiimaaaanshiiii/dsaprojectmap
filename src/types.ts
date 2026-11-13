export type Coordinate = {
  latitude: number;
  longitude: number;
  label: string;
};

export type Objective = 'fastest' | 'shortest';

export type Place = Coordinate & { id: number };

export type RouteGeometry = {
  type: 'LineString';
  coordinates: [number, number][];
};

export type Maneuver = {
  instruction: string;
  coordinate: [number, number];
  metersToNext: number;
};

export type RouteResponse = {
  route: {
    objective: Objective;
    algorithm: string;
    distanceMeters: number;
    durationSeconds: number;
    visitedNodes: number;
  };
  geometry: RouteGeometry;
  maneuvers: Maneuver[];
};
