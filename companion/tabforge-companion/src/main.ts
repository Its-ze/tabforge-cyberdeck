import { Geolocation, type Position } from '@capacitor/geolocation';
import { createIcons, icons } from 'lucide';
import './styles.css';

const STORAGE_KEY = 'tabforge-companion.v1';
const DEFAULT_TAB_URL = 'http://192.168.4.1';
const CATALOG_URL = 'https://its-ze.github.io/tabforge-cyberdeck/app-store.json';
const APK_VERSION = '0.1.0';

type JsonMap = Record<string, unknown>;

interface StoreApp {
  id: string;
  name: string;
  summary: string;
  version: string;
  category?: string;
  minFirmware?: string;
  releaseNotes?: string;
  packageUrl?: string;
  sha256?: string;
  size?: number;
}

interface StoreCatalog {
  apps?: StoreApp[];
  updated?: string;
  notes?: string;
}

interface SavedState {
  tabUrl: string;
  token: string;
  phoneName: string;
  autoShare: boolean;
}

const state: SavedState = loadState();
let catalogApps: StoreApp[] = [];
let lastPosition: Position | null = null;
let diagnostics: JsonMap | null = null;
let autoShareTimer: number | undefined;

function loadState(): SavedState {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<SavedState>;
      return {
        tabUrl: normalizeTabUrl(parsed.tabUrl || DEFAULT_TAB_URL),
        token: parsed.token || '',
        phoneName: parsed.phoneName || 'Phone',
        autoShare: Boolean(parsed.autoShare),
      };
    }
  } catch {
    localStorage.removeItem(STORAGE_KEY);
  }
  return {
    tabUrl: DEFAULT_TAB_URL,
    token: '',
    phoneName: 'Phone',
    autoShare: false,
  };
}

function saveState(): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
}

function normalizeTabUrl(value: string): string {
  const trimmed = value.trim().replace(/\/+$/, '');
  if (!trimmed) {
    return DEFAULT_TAB_URL;
  }
  if (trimmed.startsWith('http://') || trimmed.startsWith('https://')) {
    return trimmed;
  }
  return `http://${trimmed}`;
}

function byId<T extends HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!element) {
    throw new Error(`Missing element #${id}`);
  }
  return element as T;
}

function setText(id: string, text: string): void {
  byId(id).textContent = text;
}

function setBusy(buttonId: string, busy: boolean): void {
  const button = byId<HTMLButtonElement>(buttonId);
  button.disabled = busy;
  button.classList.toggle('busy', busy);
}

function logLine(message: string): void {
  const log = byId('eventLog');
  const item = document.createElement('div');
  item.textContent = `${new Date().toLocaleTimeString()}  ${message}`;
  log.prepend(item);
  while (log.children.length > 16) {
    log.lastElementChild?.remove();
  }
}

async function tabGet(path: string): Promise<JsonMap> {
  const response = await fetchWithTimeout(`${state.tabUrl}${path}`, {
    method: 'GET',
    cache: 'no-store',
  });
  return parseResponse(response);
}

async function tabPost(path: string, body: JsonMap = {}): Promise<JsonMap> {
  const payload: JsonMap = { ...body };
  if (state.token) {
    payload.token = state.token;
  }
  const response = await fetchWithTimeout(`${state.tabUrl}${path}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'text/plain;charset=utf-8',
    },
    body: JSON.stringify(payload),
  });
  return parseResponse(response);
}

async function fetchWithTimeout(url: string, init: RequestInit, timeoutMs = 7000): Promise<Response> {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(url, {
      ...init,
      signal: controller.signal,
    });
  } finally {
    window.clearTimeout(timer);
  }
}

async function parseResponse(response: Response): Promise<JsonMap> {
  const text = await response.text();
  let body: JsonMap = {};
  if (text) {
    body = JSON.parse(text) as JsonMap;
  }
  if (!response.ok || body.ok === false) {
    const detail = typeof body.detail === 'string' && body.detail ? body.detail : response.statusText;
    throw new Error(detail);
  }
  return body;
}

function renderShell(): void {
  byId('app').innerHTML = `
    <section class="phone-shell">
      <header class="topbar">
        <div class="identity">
          <span class="avatar"><i data-lucide="radio-tower"></i></span>
          <div>
            <h1>TabForge</h1>
            <p>Companion APK ${APK_VERSION}</p>
          </div>
        </div>
        <button id="diagnosticsButton" class="icon-button" aria-label="Refresh diagnostics">
          <i data-lucide="activity"></i>
        </button>
      </header>

      <section class="hero">
        <div>
          <span class="eyebrow">Phone link</span>
          <h2 id="connectionTitle">Not paired</h2>
          <p id="connectionDetail">Set the Tab IP and pair this phone.</p>
        </div>
        <div class="signal-stack">
          <span id="wifiChip" class="chip">Wi-Fi --</span>
          <span id="gpsChip" class="chip">GPS --</span>
          <span id="appsChip" class="chip">Apps --</span>
        </div>
      </section>

      <nav class="tabbar">
        <button class="tab active" data-view="connect"><i data-lucide="smartphone"></i><span>Pair</span></button>
        <button class="tab" data-view="location"><i data-lucide="map-pin"></i><span>GPS</span></button>
        <button class="tab" data-view="apps"><i data-lucide="package"></i><span>Apps</span></button>
        <button class="tab" data-view="updates"><i data-lucide="download-cloud"></i><span>Update</span></button>
        <button class="tab" data-view="diag"><i data-lucide="scan-line"></i><span>Diag</span></button>
      </nav>

      <section id="view-connect" class="view active">
        <div class="panel">
          <label>Tab API URL</label>
          <div class="row-input">
            <input id="tabUrlInput" inputmode="url" autocomplete="off" />
            <button id="saveUrlButton" class="icon-button" aria-label="Save Tab URL"><i data-lucide="save"></i></button>
          </div>
          <label>Phone name</label>
          <input id="phoneNameInput" autocomplete="nickname" />
          <div class="button-row">
            <button id="pairButton"><i data-lucide="link"></i>Pair</button>
            <button id="statusButton" class="secondary"><i data-lucide="refresh-cw"></i>Status</button>
          </div>
        </div>
      </section>

      <section id="view-location" class="view">
        <div class="panel location-panel">
          <div class="stat-line"><span>Latitude</span><strong id="latValue">--</strong></div>
          <div class="stat-line"><span>Longitude</span><strong id="lonValue">--</strong></div>
          <div class="stat-line"><span>Accuracy</span><strong id="accuracyValue">--</strong></div>
          <div class="stat-line"><span>Last sent</span><strong id="lastSentValue">--</strong></div>
          <div class="button-row">
            <button id="shareLocationButton"><i data-lucide="satellite"></i>Send GPS</button>
            <button id="getLocationButton" class="secondary"><i data-lucide="locate-fixed"></i>Refresh</button>
          </div>
          <label class="switch-row">
            <input id="autoShareToggle" type="checkbox" />
            <span>Auto share every 15 seconds</span>
          </label>
        </div>
      </section>

      <section id="view-apps" class="view">
        <div class="panel">
          <div class="button-row">
            <button id="loadCatalogButton"><i data-lucide="cloud"></i>Load Library</button>
            <button id="tabFetchStoreButton" class="secondary"><i data-lucide="refresh-cw"></i>Sync Tab Store</button>
          </div>
          <p id="catalogMeta" class="muted">Catalog not loaded.</p>
        </div>
        <div id="appList" class="app-list"></div>
      </section>

      <section id="view-updates" class="view">
        <div class="panel">
          <div class="stat-line"><span>Tab firmware</span><strong id="firmwareValue">--</strong></div>
          <div class="stat-line"><span>Latest</span><strong id="latestValue">--</strong></div>
          <div class="stat-line"><span>OTA state</span><strong id="otaStateValue">--</strong></div>
          <div class="button-row">
            <button id="checkUpdateButton"><i data-lucide="search-check"></i>Check</button>
            <button id="applyUpdateButton" class="secondary"><i data-lucide="download"></i>Apply OTA</button>
          </div>
        </div>
      </section>

      <section id="view-diag" class="view">
        <div class="panel diagnostic-grid">
          <div><span>Tab IP</span><strong id="diagIp">--</strong></div>
          <div><span>Battery</span><strong id="diagBattery">--</strong></div>
          <div><span>SD</span><strong id="diagSd">--</strong></div>
          <div><span>Mesh</span><strong id="diagMesh">--</strong></div>
          <div><span>USB</span><strong id="diagUsb">--</strong></div>
          <div><span>GPS</span><strong id="diagGps">--</strong></div>
        </div>
        <pre id="diagnosticsJson" class="json-box">{}</pre>
      </section>

      <section class="event-panel">
        <div class="event-header">
          <span>Activity</span>
          <button id="clearLogButton" class="mini-button">Clear</button>
        </div>
        <div id="eventLog"></div>
      </section>
    </section>
  `;

  byId<HTMLInputElement>('tabUrlInput').value = state.tabUrl;
  byId<HTMLInputElement>('phoneNameInput').value = state.phoneName;
  byId<HTMLInputElement>('autoShareToggle').checked = state.autoShare;
  createIcons({ icons });
}

function bindEvents(): void {
  document.querySelectorAll<HTMLButtonElement>('.tab').forEach((tab) => {
    tab.addEventListener('click', () => {
      const view = tab.dataset.view || 'connect';
      document.querySelectorAll('.tab').forEach((item) => item.classList.remove('active'));
      document.querySelectorAll('.view').forEach((item) => item.classList.remove('active'));
      tab.classList.add('active');
      byId(`view-${view}`).classList.add('active');
    });
  });

  byId('saveUrlButton').addEventListener('click', saveUrl);
  byId('pairButton').addEventListener('click', () => void pairPhone());
  byId('statusButton').addEventListener('click', () => void refreshStatus());
  byId('diagnosticsButton').addEventListener('click', () => void refreshDiagnostics());
  byId('getLocationButton').addEventListener('click', () => void refreshLocation());
  byId('shareLocationButton').addEventListener('click', () => void shareLocation(true));
  byId('loadCatalogButton').addEventListener('click', () => void loadCatalog());
  byId('tabFetchStoreButton').addEventListener('click', () => void fetchStoreOnTab());
  byId('checkUpdateButton').addEventListener('click', () => void checkUpdates());
  byId('applyUpdateButton').addEventListener('click', () => void applyUpdate());
  byId('clearLogButton').addEventListener('click', () => {
    byId('eventLog').innerHTML = '';
  });
  byId<HTMLInputElement>('autoShareToggle').addEventListener('change', (event) => {
    state.autoShare = (event.currentTarget as HTMLInputElement).checked;
    saveState();
    configureAutoShare();
  });
}

function saveUrl(): void {
  state.tabUrl = normalizeTabUrl(byId<HTMLInputElement>('tabUrlInput').value);
  state.phoneName = byId<HTMLInputElement>('phoneNameInput').value.trim() || 'Phone';
  byId<HTMLInputElement>('tabUrlInput').value = state.tabUrl;
  byId<HTMLInputElement>('phoneNameInput').value = state.phoneName;
  saveState();
  logLine(`Saved Tab URL ${state.tabUrl}`);
}

async function pairPhone(): Promise<void> {
  setBusy('pairButton', true);
  try {
    saveUrl();
    const result = await tabPost('/api/pair', {
      name: state.phoneName,
      appVersion: APK_VERSION,
    });
    state.token = String(result.token || '');
    saveState();
    logLine(`Paired with TabForge ${String(result.version || '')}`);
    await refreshStatus();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('pairButton', false);
  }
}

async function refreshStatus(): Promise<void> {
  setBusy('statusButton', true);
  try {
    diagnostics = await tabGet('/api/status');
    renderDiagnostics();
    logLine('Status refreshed.');
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('statusButton', false);
  }
}

async function refreshDiagnostics(): Promise<void> {
  setBusy('diagnosticsButton', true);
  try {
    diagnostics = await tabGet('/api/diagnostics');
    renderDiagnostics();
    logLine('Diagnostics refreshed.');
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('diagnosticsButton', false);
  }
}

function renderDiagnostics(): void {
  if (!diagnostics) {
    return;
  }

  const wifi = diagnostics.wifi as JsonMap | undefined;
  const hardware = diagnostics.hardware as JsonMap | undefined;
  const mesh = diagnostics.mesh as JsonMap | undefined;
  const phoneLocation = diagnostics.phoneLocation as JsonMap | undefined;
  const apps = diagnostics.apps as JsonMap | undefined;
  const update = diagnostics.update as JsonMap | undefined;

  const ip = String(diagnostics.ip || '--');
  const version = String(diagnostics.version || '--');
  setText('connectionTitle', state.token ? 'Paired to TabForge' : 'Tab reachable');
  setText('connectionDetail', `${state.tabUrl}  ${version}`);
  setText('wifiChip', `${String(wifi?.state || 'Wi-Fi')} ${ip}`);
  setText('gpsChip', phoneLocation?.ready ? 'GPS shared' : 'GPS waiting');
  setText('appsChip', `${String(apps?.installedCount ?? '--')} apps`);

  setText('diagIp', ip);
  setText('diagBattery', String(hardware?.battery || '--'));
  setText('diagSd', hardware?.sdReady ? 'ready' : 'missing');
  setText('diagMesh', `${String(mesh?.mode || '--')} ${String(mesh?.transport || '')}`);
  setText('diagUsb', String(hardware?.usb || '--'));
  setText('diagGps', phoneLocation?.ready ? `${Number(phoneLocation.accuracyM || 0).toFixed(1)} m` : '--');
  setText('firmwareValue', String(update?.currentVersion || version));
  setText('latestValue', String(update?.latestVersion || '--'));
  setText('otaStateValue', String(update?.state || '--'));
  byId('diagnosticsJson').textContent = JSON.stringify(diagnostics, null, 2);
}

async function refreshLocation(): Promise<Position> {
  setBusy('getLocationButton', true);
  try {
    await Geolocation.requestPermissions();
    const position = await Geolocation.getCurrentPosition({
      enableHighAccuracy: true,
      timeout: 15000,
      maximumAge: 0,
    });
    lastPosition = position;
    renderPosition(position, false);
    logLine('Phone GPS refreshed.');
    return position;
  } finally {
    setBusy('getLocationButton', false);
  }
}

function renderPosition(position: Position, sent: boolean): void {
  setText('latValue', position.coords.latitude.toFixed(7));
  setText('lonValue', position.coords.longitude.toFixed(7));
  setText('accuracyValue', `${Math.round(position.coords.accuracy)} m`);
  if (sent) {
    setText('lastSentValue', new Date().toLocaleTimeString());
  }
}

async function shareLocation(showBusy: boolean): Promise<void> {
  if (showBusy) {
    setBusy('shareLocationButton', true);
  }
  try {
    const position = lastPosition || (await refreshLocation());
    const payload: JsonMap = {
      lat: position.coords.latitude,
      lon: position.coords.longitude,
      accuracy: position.coords.accuracy,
      altitude: position.coords.altitude ?? 0,
      speed: position.coords.speed ?? 0,
      heading: position.coords.heading ?? 0,
      timestamp: new Date(position.timestamp).toISOString(),
      source: 'android-companion',
    };
    await tabPost('/api/location', payload);
    renderPosition(position, true);
    logLine(`Sent phone GPS ${position.coords.latitude.toFixed(7)}, ${position.coords.longitude.toFixed(7)}`);
    await refreshDiagnostics();
  } catch (error) {
    showConnectionError(error);
  } finally {
    if (showBusy) {
      setBusy('shareLocationButton', false);
    }
  }
}

function configureAutoShare(): void {
  if (autoShareTimer !== undefined) {
    window.clearInterval(autoShareTimer);
    autoShareTimer = undefined;
  }
  if (state.autoShare) {
    autoShareTimer = window.setInterval(() => {
      void shareLocation(false);
    }, 15000);
    logLine('Auto GPS sharing enabled.');
  }
}

async function loadCatalog(): Promise<void> {
  setBusy('loadCatalogButton', true);
  try {
    const response = await fetch(CATALOG_URL, { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(response.statusText);
    }
    const catalog = (await response.json()) as StoreCatalog;
    catalogApps = catalog.apps || [];
    setText('catalogMeta', `${catalogApps.length} apps  ${catalog.updated || ''}`);
    renderCatalog();
    logLine(`Loaded ${catalogApps.length} apps from GitHub.`);
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('loadCatalogButton', false);
  }
}

function renderCatalog(): void {
  const appList = byId('appList');
  appList.innerHTML = '';
  if (catalogApps.length === 0) {
    appList.innerHTML = '<p class="empty">Load the app library to see TabForge apps.</p>';
    return;
  }

  for (const app of catalogApps) {
    const item = document.createElement('article');
    item.className = 'app-card';
    item.innerHTML = `
      <div class="app-card-head">
        <span class="app-icon">${(app.category || 'APP').slice(0, 3).toUpperCase()}</span>
        <div>
          <h3>${escapeHtml(app.name)}</h3>
          <p>${escapeHtml(app.category || 'TabForge')} ${escapeHtml(app.version)}</p>
        </div>
      </div>
      <p>${escapeHtml(app.summary)}</p>
      <div class="app-meta">
        <span>Min ${escapeHtml(app.minFirmware || '--')}</span>
        <span>${formatBytes(app.size || 0)}</span>
      </div>
      <button data-install="${escapeHtml(app.id)}"><i data-lucide="download"></i>Install / Update</button>
    `;
    appList.appendChild(item);
  }

  appList.querySelectorAll<HTMLButtonElement>('button[data-install]').forEach((button) => {
    button.addEventListener('click', () => {
      const id = button.dataset.install || '';
      void installApp(id, button);
    });
  });
  createIcons({ icons });
}

async function fetchStoreOnTab(): Promise<void> {
  setBusy('tabFetchStoreButton', true);
  try {
    await tabPost('/api/apps/fetch');
    logLine('Asked Tab to fetch the store catalog.');
    window.setTimeout(() => void refreshDiagnostics(), 1500);
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('tabFetchStoreButton', false);
  }
}

async function installApp(id: string, button: HTMLButtonElement): Promise<void> {
  button.disabled = true;
  try {
    await tabPost('/api/apps/install', { id });
    logLine(`Asked Tab to install/update ${id}.`);
    window.setTimeout(() => void refreshDiagnostics(), 2500);
  } catch (error) {
    showConnectionError(error);
  } finally {
    button.disabled = false;
  }
}

async function checkUpdates(): Promise<void> {
  setBusy('checkUpdateButton', true);
  try {
    const result = await tabPost('/api/update/check');
    logLine(result.updateAvailable ? 'Firmware update is available.' : `Update check: ${String(result.result || 'current')}`);
    diagnostics = await tabGet('/api/status');
    renderDiagnostics();
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('checkUpdateButton', false);
  }
}

async function applyUpdate(): Promise<void> {
  setBusy('applyUpdateButton', true);
  try {
    await tabPost('/api/update/apply');
    logLine('Started Tab OTA update task.');
    window.setTimeout(() => void refreshDiagnostics(), 3000);
  } catch (error) {
    showConnectionError(error);
  } finally {
    setBusy('applyUpdateButton', false);
  }
}

function showConnectionError(error: unknown): void {
  const message = error instanceof Error ? error.message : String(error);
  setText('connectionTitle', 'Needs attention');
  setText('connectionDetail', message);
  logLine(`Error: ${message}`);
}

function formatBytes(value: number): string {
  if (value <= 0) {
    return '--';
  }
  if (value < 1024) {
    return `${value} B`;
  }
  return `${(value / 1024).toFixed(1)} KB`;
}

function escapeHtml(value: string): string {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

renderShell();
bindEvents();
configureAutoShare();
if (state.token) {
  void refreshStatus();
} else {
  logLine('Enter the Tab IP, then pair this phone.');
}
