export class LocalStore {
  constructor(device) { this.devices = new Map([[device.id, { firmwareChannel: "stable", lastFirmwareVersion: 0, ...device }]]); this.pairings = new Map(); this.sessions = new Map(); this.snapshots = new Map(); this.airlines = new Map(); this.aircraft = new Map(); this.trackedFlights = new Map(); this.flightStatuses = new Map(); this.flightRoutes = new Map(); this.providerUsage = new Map(); this.devicePasskeys = new Map(); this.deviceChallenges = new Map(); this.adminPasskeys = new Map(); this.adminChallenges = new Map(); this.adminSessions = new Map(); }
  async findDeviceByKeyHash(keyHash, now) { return [...this.devices.values()].find(d => d.apiKeyHash === keyHash || (d.previousApiKeyHash === keyHash && d.previousKeyExpiresAt > now)) || null; }
  async getDevice(id) { return this.devices.get(id) || null; }
  async listDevices() { return [...this.devices.values()].sort((a, b) => a.name.localeCompare(b.name)); }
  async setFirmwareChannel(id, firmwareChannel) { const device = this.devices.get(id); if (!device) return null; device.firmwareChannel = firmwareChannel; return device; }
  async noteDeviceCheckin(id, version, now) { const device = this.devices.get(id); if (device) Object.assign(device, { lastFirmwareVersion: version, lastSeenAt: now }); }
  async getSnapshot(id) { return this.snapshots.get(id) || null; }
  async putSnapshot(id, payload, fetchedAt) { this.snapshots.set(id, { payload, fetchedAt }); }
  async deleteSnapshot(id) { this.snapshots.delete(id); }
  async deleteAllSnapshots() { this.snapshots.clear(); }
  async createPairing(tokenHash, deviceId, expiresAt) { this.pairings.set(tokenHash, { deviceId, expiresAt, used: false }); }
  async consumePairing(tokenHash, now) { const p = this.pairings.get(tokenHash); if (!p || p.used || p.expiresAt <= now) return null; p.used = true; return p.deviceId; }
  async createSession(tokenHash, deviceId, expiresAt) { this.sessions.set(tokenHash, { deviceId, expiresAt }); }
  async findDeviceBySessionHash(tokenHash, now) { const s = this.sessions.get(tokenHash); return s && s.expiresAt > now ? this.devices.get(s.deviceId) : null; }
  async updateSettings(id, settings) { Object.assign(this.devices.get(id), settings); }
  async requestRotation(id) { this.devices.get(id).rotationRequested = true; }
  async rotateKey(id, nextKeyHash, priorKeyHash, previousKeyExpiresAt) { Object.assign(this.devices.get(id), { apiKeyHash: nextKeyHash, previousApiKeyHash: priorKeyHash, previousKeyExpiresAt, rotationRequested: false }); }
  async deleteSession(tokenHash) { this.sessions.delete(tokenHash); }
  async countDevicePasskeys() { return this.devicePasskeys.size; }
  async getDevicePasskeys(deviceId) { return [...this.devicePasskeys.values()].filter(entry => entry.deviceId === deviceId); }
  async getDevicePasskey(id) { return this.devicePasskeys.get(id) || null; }
  async addDevicePasskey(entry) { this.devicePasskeys.set(entry.credentialId, entry); }
  async updateDevicePasskeyCounter(id, counter, now) { const entry = this.devicePasskeys.get(id); if (entry) Object.assign(entry, { counter, lastUsedAt: now }); }
  async createDeviceWebauthnChallenge(challenge, deviceId, kind, expiresAt) { this.deviceChallenges.set(challenge, { deviceId, kind, expiresAt }); }
  async consumeDeviceWebauthnChallenge(challenge, kind, now) { const entry = this.deviceChallenges.get(challenge); this.deviceChallenges.delete(challenge); return entry?.kind === kind && entry.expiresAt > now ? { deviceId: entry.deviceId } : null; }
  async countAdminPasskeys() { return this.adminPasskeys.size; }
  async getAdminPasskeys() { return [...this.adminPasskeys.values()]; }
  async getAdminPasskey(id) { return this.adminPasskeys.get(id) || null; }
  async addAdminPasskey(entry) { this.adminPasskeys.set(entry.credentialId, entry); }
  async updateAdminPasskeyCounter(id, counter, now) { const entry = this.adminPasskeys.get(id); if (entry) Object.assign(entry, { counter, lastUsedAt: now }); }
  async createWebauthnChallenge(challenge, kind, expiresAt) { this.adminChallenges.set(challenge, { kind, expiresAt }); }
  async consumeWebauthnChallenge(challenge, kind, now) { const entry = this.adminChallenges.get(challenge); this.adminChallenges.delete(challenge); return entry?.kind === kind && entry.expiresAt > now ? challenge : null; }
  async createAdminSession(tokenHash, credentialId, expiresAt) { this.adminSessions.set(tokenHash, { credentialId, expiresAt }); }
  async findAdminSession(tokenHash, now) { const entry = this.adminSessions.get(tokenHash); return entry?.expiresAt > now ? entry : null; }
  async deleteAdminSession(tokenHash) { this.adminSessions.delete(tokenHash); }
  async getAirline(code) { return this.airlines.get(code) || null; }
  async putAirline(code, airline) { this.airlines.set(code, airline); }
  async getAircraft(icao24) { return this.aircraft.get(icao24) || null; }
  async putAircraft(icao24, aircraft) { this.aircraft.set(icao24, aircraft); }
  async getTrackedFlights(deviceId, flightDate) { return [...this.trackedFlights.values()].filter(entry => (!deviceId || entry.deviceId === deviceId) && (!flightDate || entry.flightDate === flightDate)).map(entry => ({ ...entry, identifier: entry.identifier || entry.flightNumber, identifierType: entry.identifierType || "flightNumber" })); }
  async addTrackedFlight(entry) { this.trackedFlights.set(entry.id, entry); }
  async deleteTrackedFlight(deviceId, id) { const entry = this.trackedFlights.get(id); if (entry?.deviceId !== deviceId) return false; this.trackedFlights.delete(id); return true; }
  async deleteTrackedFlightAny(id) { return this.trackedFlights.delete(id); }
  async getFlightStatus(id) { return this.flightStatuses.get(id) || null; }
  async putFlightStatus(id, status) { this.flightStatuses.set(id, status); }
  async deleteFlightStatus(id) { this.flightStatuses.delete(id); }
  async getFlightRoute(key) { return this.flightRoutes.get(key) || null; }
  async putFlightRoute(key, route) { this.flightRoutes.set(key, route); }
  async takeProviderQuota(provider, month, limit) { const key = `${provider}:${month}`, used = this.providerUsage.get(key) || 0; if (used >= limit) return false; this.providerUsage.set(key, used + 1); return true; }
}
