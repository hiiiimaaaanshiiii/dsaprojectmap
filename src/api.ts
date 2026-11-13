import type { Coordinate, Objective, Place, RouteResponse } from './types';

const engineUnavailableMessage = 'The C++ routing engine is not running. In this project folder, run “npm run dev:full”, then refresh this page.';

async function parseResponse<T>(response: Response): Promise<T> {
  const body = await response.text();
  let payload: { error?: string } | null = null;
  try {
    payload = body ? JSON.parse(body) as { error?: string } : null;
  } catch {
    throw new Error(response.status >= 500 ? engineUnavailableMessage : 'The routing service returned an invalid response.');
  }
  if (!response.ok) throw new Error(payload?.error ?? engineUnavailableMessage);
  if (payload === null) throw new Error(engineUnavailableMessage);
  return payload as T;
}

export async function searchPlaces(query: string): Promise<Place[]> {
  try {
    const response = await fetch(`/api/search?q=${encodeURIComponent(query)}`);
    const payload = await parseResponse<{ results: Array<{ id: number; name: string; latitude: number; longitude: number }> }>(response);
    return payload.results.map((place) => ({
      id: place.id,
      label: place.name,
      latitude: place.latitude,
      longitude: place.longitude,
    }));
  } catch {
    return [];
  }
}

export async function getRoute(origin: Coordinate, destination: Coordinate, objective: Objective): Promise<RouteResponse> {
  const query = new URLSearchParams({
    fromLat: origin.latitude.toString(),
    fromLng: origin.longitude.toString(),
    toLat: destination.latitude.toString(),
    toLng: destination.longitude.toString(),
    objective,
  });
  try {
    const response = await fetch(`/api/route?${query}`);
    return parseResponse<RouteResponse>(response);
  } catch (error) {
    if (error instanceof Error && error.message !== 'Failed to fetch') throw error;
    throw new Error(engineUnavailableMessage);
  }
}
