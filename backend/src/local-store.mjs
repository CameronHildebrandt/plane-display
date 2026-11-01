export class LocalStore {
  constructor(device) { this.devices = new Map([[device.id, device]]); this.pairings = new Map(); this.sessions = new Map(); this.snapshots = new Map(); this.airlines = new Map(); this.aircraft = new Map(); this.trackedFlights = new Map(); this.flightStatuses = new Map(); this.providerUsage = new Map(); }
  async findDeviceByKeyHash(keyHash, now) { return [...this.devices.values()].find(d => d.apiKeyHash === keyHash || (d.previousApiKeyHash === keyHash && d.previousKeyExpiresAt > now)) || null; }
  async getSnapshot(id) { return this.snapshots.get(id) || null; }
  async putSnapshot(id, payload, fetchedAt) { this.snapshots.set(id, { payload, fetchedAt }); }
  async deleteSnapshot(id) { this.snapshots.delete(id); }
  async createPairing(tokenHash, deviceId, expiresAt) { this.pairings.set(tokenHash, { deviceId, expiresAt, used: false }); }
  async consumePairing(tokenHash, now) { const p = this.pairings.get(tokenHash); if (!p || p.used || p.expiresAt <= now) return null; p.used = true; return p.deviceId; }
  async createSession(tokenHash, deviceId, expiresAt) { this.sessions.set(tokenHash, { deviceId, expiresAt }); }
  async findDeviceBySessionHash(tokenHash, now) { const s = this.sessions.get(tokenHash); return s && s.expiresAt > now ? this.devices.get(s.deviceId) : null; }
  async updateSettings(id, settings) { Object.assign(this.devices.get(id), settings); }
  async requestRotation(id) { this.devices.get(id).rotationRequested = true; }
  async rotateKey(id, nextKeyHash, priorKeyHash, previousKeyExpiresAt) { Object.assign(this.devices.get(id), { apiKeyHash: nextKeyHash, previousApiKeyHash: priorKeyHash, previousKeyExpiresAt, rotationRequested: false }); }
  async deleteSession(tokenHash) { this.sessions.delete(tokenHash); }
  async getAirline(code) { return this.airlines.get(code) || null; }
  async putAirline(code, airline) { this.airlines.set(code, airline); }
  async getAircraft(icao24) { return this.aircraft.get(icao24) || null; }
  async putAircraft(icao24, aircraft) { this.aircraft.set(icao24, aircraft); }
  async getTrackedFlights(deviceId, flightDate) { return [...this.trackedFlights.values()].filter(entry => entry.deviceId === deviceId && (!flightDate || entry.flightDate === flightDate)).map(entry => ({ ...entry, identifier: entry.identifier || entry.flightNumber, identifierType: entry.identifierType || "flightNumber" })); }
  async addTrackedFlight(entry) { this.trackedFlights.set(entry.id, entry); }
  async deleteTrackedFlight(deviceId, id) { const entry = this.trackedFlights.get(id); if (entry?.deviceId === deviceId) this.trackedFlights.delete(id); }
  async getFlightStatus(id) { return this.flightStatuses.get(id) || null; }
  async putFlightStatus(id, status) { this.flightStatuses.set(id, status); }
  async takeProviderQuota(provider, month, limit) { const key = `${provider}:${month}`, used = this.providerUsage.get(key) || 0; if (used >= limit) return false; this.providerUsage.set(key, used + 1); return true; }
}
