const DATA = {
  metrics: {
    totalServices: 12,
    validServices: 10,
    activeProjects: 8,
    projectTrend: "+2",
    runningInstances: 15,
    totalInstances: 23,
    failuresToday: 2,
    daemonCount: 4,
    daemonActive: 3,
    fixedRateCount: 3,
    nextScheduledRun: "14:30",
    manualCount: 5,
    lastManualRun: "2h ago"
  },
  services: [
    { id: "svc-3dvision", name: "3D Vision Service", version: "2.1.0", valid: true, commandCount: 5, projectCount: 2, dir: "drivers/3dvision", manifest: { runtime: "node", restart: "on-failure", timeoutMs: 8000 } },
    { id: "svc-modbusrtu", name: "Modbus RTU Service", version: "1.4.2", valid: true, commandCount: 3, projectCount: 1, dir: "drivers/modbus_rtu", manifest: { runtime: "native", restart: "always", timeoutMs: 5000 } },
    { id: "svc-modbustcp", name: "Modbus TCP Service", version: "1.4.2", valid: true, commandCount: 3, projectCount: 1, dir: "drivers/modbus_tcp", manifest: { runtime: "native", restart: "always", timeoutMs: 5000 } },
    { id: "svc-aggregator", name: "Aggregator Service", version: "0.9.0", valid: false, commandCount: 4, projectCount: 1, dir: "services/aggregator", manifest: { runtime: "node", restart: "never", timeoutMs: 12000 } }
  ],
  projects: [
    { id: "proj-001", name: "Vision Pipeline", serviceId: "svc-3dvision", serviceName: "3D Vision Service", valid: true, autoStart: true, scheduleType: "daemon", instanceCount: 3 },
    { id: "proj-002", name: "Modbus Poller", serviceId: "svc-modbusrtu", serviceName: "Modbus RTU Service", valid: true, autoStart: true, scheduleType: "fixed_rate", instanceCount: 1 },
    { id: "proj-003", name: "TCP Gateway", serviceId: "svc-modbustcp", serviceName: "Modbus TCP Service", valid: true, autoStart: false, scheduleType: "manual", instanceCount: 0 },
    { id: "proj-004", name: "Data Aggregator", serviceId: "svc-aggregator", serviceName: "Aggregator Service", valid: false, autoStart: false, scheduleType: "manual", instanceCount: 0 },
    { id: "proj-005", name: "Alert Monitor", serviceId: "svc-3dvision", serviceName: "3D Vision Service", valid: true, autoStart: true, scheduleType: "daemon", instanceCount: 2 },
    { id: "proj-006", name: "Log Collector", serviceId: "svc-modbusrtu", serviceName: "Modbus RTU Service", valid: true, autoStart: false, scheduleType: "fixed_rate", instanceCount: 1 }
  ],
  instances: [
    { id: "inst-001", projectId: "proj-001", projectName: "Vision Pipeline", serviceName: "3D Vision Service", pid: 12847, status: "running", start: "2d ago", uptime: "2d 14h", cpu: "12%", mem: "256 MB" },
    { id: "inst-002", projectId: "proj-001", projectName: "Vision Pipeline", serviceName: "3D Vision Service", pid: 12848, status: "running", start: "2d ago", uptime: "2d 14h", cpu: "8%", mem: "198 MB" },
    { id: "inst-003", projectId: "proj-002", projectName: "Modbus Poller", serviceName: "Modbus RTU Service", pid: 13201, status: "running", start: "5d ago", uptime: "5d 2h", cpu: "3%", mem: "64 MB" },
    { id: "inst-004", projectId: "proj-004", projectName: "Data Aggregator", serviceName: "Aggregator Service", pid: 0, status: "stopped", start: "-", uptime: "-", cpu: "-", mem: "-" },
    { id: "inst-005", projectId: "proj-005", projectName: "Alert Monitor", serviceName: "3D Vision Service", pid: 14002, status: "error", start: "3m ago", uptime: "0h 3m", cpu: "0%", mem: "33 MB" }
  ],
  drivers: [
    { id: "drv-3dvision", name: "driver_3dvision", path: "bin/driver_3dvision", version: "2.1.0", commands: 5, status: "available" },
    { id: "drv-modbusrtu", name: "driver_modbusrtu", path: "bin/driver_modbusrtu", version: "1.4.2", commands: 3, status: "available" },
    { id: "drv-modbustcp", name: "driver_modbustcp", path: "bin/driver_modbustcp", version: "1.4.2", commands: 3, status: "available" },
    { id: "drv-aggregator", name: "driver_aggregator", path: "bin/driver_aggregator", version: "0.9.0", commands: 4, status: "unavailable" }
  ],
  events: [
    { type: "success", icon: "▶", message: "Vision Pipeline instance started (PID 12848)", time: "5 min ago" },
    { type: "error", icon: "✕", message: "Alert Monitor instance crashed (exit code 139)", time: "12 min ago" },
    { type: "warning", icon: "⏲", message: "Log Collector scheduled execution triggered", time: "30 min ago" },
    { type: "success", icon: "✓", message: "TCP Gateway project validated", time: "1 h ago" },
    { type: "info", icon: "↻", message: "Service scan completed: 12 services found", time: "2 h ago" },
    { type: "info", icon: "+", message: "Project \"Log Collector\" created", time: "5 h ago" }
  ],
  logs: {
    "inst-001": [
      { level: "info", msg: "[10:13:32] boot: loading service manifest" },
      { level: "debug", msg: "[10:13:33] config: frame_rate=20, model=vision_v4" },
      { level: "info", msg: "[10:13:36] worker: 3 pipelines online" },
      { level: "warning", msg: "[10:14:00] queue: backlog increased to 41" },
      { level: "info", msg: "[10:14:02] autoscale: +1 worker" },
      { level: "info", msg: "[10:14:08] heartbeat: latency=23ms" }
    ],
    "inst-002": [
      { level: "info", msg: "[10:12:00] boot: service ready" },
      { level: "debug", msg: "[10:12:10] poll: 8 cameras connected" },
      { level: "info", msg: "[10:12:20] stream: fps=19.8" }
    ],
    "inst-003": [
      { level: "info", msg: "[10:10:11] serial: /dev/tty.usbmodem opened" },
      { level: "info", msg: "[10:10:12] scan: 14 registers subscribed" },
      { level: "warning", msg: "[10:10:22] poll timeout on node 03, retry=1" }
    ],
    "inst-005": [
      { level: "info", msg: "[10:11:05] process started" },
      { level: "error", msg: "[10:11:11] unhandled exception: invalid pointer" },
      { level: "error", msg: "[10:11:11] process exit code=139" }
    ]
  }
};

const state = {
  page: "dashboard",
  serviceTab: "overview",
  selectedService: "svc-3dvision",
  projectView: "card",
  loading: false,
  logs: {
    instanceId: null,
    level: "all",
    search: "",
    autoScroll: true
  }
};

let confirmAction = null;

const pageMeta = {
  dashboard: "Dashboard / System overview",
  projects: "Projects / orchestration and lifecycle",
  services: "Services / scan and validation",
  "service-detail": "Service Detail / metadata and schema",
  "project-form": "Project Form / schema driven config",
  instances: "Instances / runtime monitor",
  drivers: "Drivers / runtime bindings",
  settings: "Settings / environment and server"
};

function getServiceById(id) {
  return DATA.services.find((s) => s.id === id) || DATA.services[0];
}

function renderLoading() {
  return `
    <div class="loading-shell">
      <div class="skeleton"></div>
      <div class="skeleton"></div>
      <div class="skeleton"></div>
      <div class="skeleton"></div>
    </div>
  `;
}

function renderAreaChart() {
  return `
    <svg viewBox="0 0 800 260" preserveAspectRatio="none" aria-hidden="true">
      <g stroke="#e7edf2" stroke-width="1">
        <line x1="40" y1="20" x2="760" y2="20"></line>
        <line x1="40" y1="75" x2="760" y2="75"></line>
        <line x1="40" y1="130" x2="760" y2="130"></line>
        <line x1="40" y1="185" x2="760" y2="185"></line>
        <line x1="40" y1="240" x2="760" y2="240"></line>
      </g>
      <polygon fill="rgba(76, 175, 80, 0.18)" points="40,188 100,162 150,171 210,142 280,148 340,130 390,136 440,128 500,141 560,121 630,134 700,125 760,150 760,240 40,240"></polygon>
      <polyline fill="none" stroke="#4caf50" stroke-width="2.3" points="40,188 100,162 150,171 210,142 280,148 340,130 390,136 440,128 500,141 560,121 630,134 700,125 760,150"></polyline>
      <polyline fill="none" stroke="#2196f3" stroke-width="2" points="40,214 100,201 150,207 210,198 280,186 340,194 390,179 440,183 500,176 560,182 630,171 700,179 760,174"></polyline>
      <polyline fill="none" stroke="#f44336" stroke-width="2" points="40,236 100,236 150,221 210,236 280,236 340,236 390,228 440,236 500,236 560,221 630,236 700,236 760,236"></polyline>
    </svg>
  `;
}

function renderDashboard() {
  const m = DATA.metrics;
  const healthRows = DATA.projects.slice(0, 5).map((p) => {
    const dot = !p.valid ? "error" : p.instanceCount > 0 ? "running" : "warning";
    return `
      <li>
        <div class="health-main">
          <span class="status-dot ${dot}"></span>
          <div>
            <div class="item-title">${p.name}</div>
            <div class="item-sub">${p.serviceName}</div>
          </div>
        </div>
        <div class="inline-actions">
          <span class="item-sub">${p.instanceCount} instances</span>
          <span class="badge ${p.valid ? "success" : "error"}">${p.valid ? "Healthy" : "Invalid"}</span>
        </div>
      </li>
    `;
  }).join("");

  const eventRows = DATA.events.map((e) => {
    return `
      <li>
        <div class="event-main">
          <span class="event-icon ${e.type}">${e.icon}</span>
          <div>
            <div class="item-title">${e.message}</div>
            <div class="item-sub">${e.time}</div>
          </div>
        </div>
      </li>
    `;
  }).join("");

  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Dashboard</h1>
          <p class="page-subtitle">System overview at a glance</p>
        </div>
        <div class="inline-actions">
          <button class="btn btn-secondary" data-action="refresh">Refresh</button>
        </div>
      </div>

      <div class="grid metric-grid">
        <article class="card metric-card">
          <div class="metric-icon primary">S</div>
          <div>
            <div class="metric-label">Total Services</div>
            <div class="metric-value">${m.totalServices}</div>
            <div class="metric-desc">${m.validServices} valid</div>
          </div>
        </article>
        <article class="card metric-card">
          <div class="metric-icon primary">P</div>
          <div>
            <div class="metric-label">Active Projects</div>
            <div class="metric-value">${m.activeProjects}</div>
            <div class="metric-desc">${m.projectTrend} vs last week</div>
          </div>
        </article>
        <article class="card metric-card">
          <div class="metric-icon success">I</div>
          <div>
            <div class="metric-label">Running Instances</div>
            <div class="metric-value">${m.runningInstances}</div>
            <div class="metric-desc">of ${m.totalInstances} total</div>
          </div>
        </article>
        <article class="card metric-card">
          <div class="metric-icon ${m.failuresToday > 0 ? "error" : "success"}">!</div>
          <div>
            <div class="metric-label">Failures Today</div>
            <div class="metric-value">${m.failuresToday}</div>
            <div class="metric-desc">auto-restart triggered</div>
          </div>
        </article>
      </div>

      <div class="grid-12 grid" style="margin-top:24px">
        <article class="card col-4">
          <div class="card-header"><h3 class="section-title">Instance Status</h3></div>
          <div class="card-body">
            <div class="status-donut">
              <div class="donut-ring">
                <div class="donut-inner"><strong>${m.totalInstances}</strong><span>Total</span></div>
              </div>
              <ul class="legend">
                <li><span class="dot" style="background:var(--success-500)"></span>Running 15</li>
                <li><span class="dot" style="background:var(--gray-400)"></span>Stopped 5</li>
                <li><span class="dot" style="background:var(--error-500)"></span>Error 2</li>
                <li><span class="dot" style="background:var(--warning-500)"></span>Pending 1</li>
              </ul>
            </div>
          </div>
        </article>

        <article class="card col-8">
          <div class="card-header">
            <h3 class="section-title">Instance Activity</h3>
            <select>
              <option>Last 1 hour</option>
              <option>Last 6 hours</option>
              <option selected>Last 24 hours</option>
              <option>Last 7 days</option>
            </select>
          </div>
          <div class="card-body">
            <div class="chart-wrap">${renderAreaChart()}</div>
          </div>
        </article>
      </div>

      <div class="grid-12 grid" style="margin-top:24px">
        <article class="card col-7">
          <div class="card-header">
            <h3 class="section-title">Project Health</h3>
            <button class="btn btn-ghost small" data-route="projects">View All</button>
          </div>
          <ul class="health-list">${healthRows}</ul>
        </article>

        <article class="card col-5">
          <div class="card-header">
            <h3 class="section-title">Recent Events</h3>
            <span class="badge info">${DATA.events.length} new</span>
          </div>
          <ul class="events">${eventRows}</ul>
        </article>
      </div>

      <article class="card" style="margin-top:24px">
        <div class="card-header"><h3 class="section-title">Schedule Overview</h3></div>
        <div class="card-body">
          <div class="schedule-grid">
            <div class="schedule-item daemon">
              <div class="glyph">D</div>
              <div class="schedule-count">${m.daemonCount}</div>
              <div class="item-title">Daemon Projects</div>
              <div class="item-sub">${m.daemonActive} active, auto-restart</div>
            </div>
            <div class="schedule-item fixed">
              <div class="glyph">T</div>
              <div class="schedule-count">${m.fixedRateCount}</div>
              <div class="item-title">Scheduled Tasks</div>
              <div class="item-sub">Next run: ${m.nextScheduledRun}</div>
            </div>
            <div class="schedule-item manual">
              <div class="glyph">M</div>
              <div class="schedule-count">${m.manualCount}</div>
              <div class="item-title">Manual Projects</div>
              <div class="item-sub">Last run: ${m.lastManualRun}</div>
            </div>
          </div>
        </div>
      </article>
    </section>
  `;
}

function renderServices() {
  const cards = DATA.services.map((s) => {
    return `
      <article class="card project-card">
        <div class="card-header">
          <div>
            <div class="item-title">${s.name}</div>
            <div class="item-sub">${s.id} / v${s.version}</div>
          </div>
          <span class="badge ${s.valid ? "success" : "error"}">${s.valid ? "Valid" : "Invalid"}</span>
        </div>
        <div class="card-body">
          <div class="info-list">
            <div class="info-line"><div class="info-key">Version</div><div class="info-val">${s.version}</div></div>
            <div class="info-line"><div class="info-key">Commands</div><div class="info-val">${s.commandCount}</div></div>
            <div class="info-line"><div class="info-key">Projects</div><div class="info-val">${s.projectCount}</div></div>
            <div class="info-line"><div class="info-key">Directory</div><div class="info-val">${s.dir}</div></div>
          </div>
        </div>
        <div class="card-footer">
          <button class="btn btn-ghost small" data-action="open-service" data-id="${s.id}">View Details</button>
        </div>
      </article>
    `;
  }).join("");

  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Services</h1>
          <p class="page-subtitle">${DATA.services.length} services available</p>
        </div>
        <div class="inline-actions">
          <button class="btn btn-secondary" data-action="refresh">Scan Services</button>
        </div>
      </div>

      <div class="filter-bar">
        <label class="search-field">
          <span class="search-icon">⌕</span>
          <input type="text" placeholder="Search services...">
        </label>
        <select>
          <option>All Status</option>
          <option>Valid</option>
          <option>Invalid</option>
        </select>
      </div>

      <div class="card-grid">
        ${cards}
      </div>
    </section>
  `;
}

function renderServiceDetail() {
  const s = getServiceById(state.selectedService);
  const projectRows = DATA.projects.filter((p) => p.serviceId === s.id).map((p) => {
    return `<tr><td>${p.name}</td><td><code>${p.id}</code></td><td>${p.instanceCount}</td><td><span class="badge ${p.valid ? "success" : "error"}">${p.valid ? "Valid" : "Invalid"}</span></td></tr>`;
  }).join("");

  const tab = state.serviceTab;
  let panel = "";
  if (tab === "overview") {
    panel = `
      <div class="dual">
        <article class="card">
          <div class="card-header"><h3 class="section-title">Service Information</h3></div>
          <div class="card-body">
            <div class="info-list">
              <div class="info-line"><div class="info-key">ID</div><div class="info-val">${s.id}</div></div>
              <div class="info-line"><div class="info-key">Name</div><div class="info-val">${s.name}</div></div>
              <div class="info-line"><div class="info-key">Version</div><div class="info-val">${s.version}</div></div>
              <div class="info-line"><div class="info-key">Directory</div><div class="info-val">${s.dir}</div></div>
            </div>
          </div>
        </article>
        <article class="card">
          <div class="card-header"><h3 class="section-title">Manifest</h3></div>
          <div class="card-body">
            <pre class="code-block" style="display:block;white-space:pre-wrap;padding:12px;line-height:1.5">${JSON.stringify(s.manifest, null, 2)}</pre>
          </div>
        </article>
      </div>
    `;
  } else if (tab === "commands") {
    panel = `
      <div class="table-wrap">
        <table class="table">
          <thead><tr><th>Command</th><th>Description</th><th>Input</th><th>Output</th></tr></thead>
          <tbody>
            <tr><td>start</td><td>Start service runtime</td><td>config</td><td>ack + pid</td></tr>
            <tr><td>stop</td><td>Stop service runtime</td><td>instanceId</td><td>ack</td></tr>
            <tr><td>status</td><td>Query runtime status</td><td>instanceId</td><td>state</td></tr>
            <tr><td>reload</td><td>Hot reload config</td><td>config patch</td><td>result</td></tr>
          </tbody>
        </table>
      </div>
    `;
  } else if (tab === "schema") {
    panel = `
      <article class="card">
        <div class="card-header"><h3 class="section-title">Configuration Schema</h3></div>
        <div class="card-body">
          <pre class="code-block" style="display:block;white-space:pre-wrap;padding:12px;line-height:1.55">{
  "fields": [
    { "name": "endpoint", "type": "String", "required": true, "ui": { "placeholder": "tcp://127.0.0.1:502" } },
    { "name": "timeoutMs", "type": "Int", "constraints": { "min": 100, "max": 30000 }, "ui": { "step": 100 } },
    { "name": "retry", "type": "Int", "constraints": { "min": 0, "max": 10 } },
    { "name": "enableMetrics", "type": "Bool" }
  ]
}</pre>
        </div>
      </article>
    `;
  } else {
    panel = `
      <div class="table-wrap">
        <table class="table">
          <thead><tr><th>Project</th><th>ID</th><th>Running</th><th>Status</th></tr></thead>
          <tbody>${projectRows || "<tr><td colspan='4'>No related projects</td></tr>"}</tbody>
        </table>
      </div>
    `;
  }

  return `
    <section>
      <div class="breadcrumb">
        <button data-route="services">Services</button>
        <span>/</span>
        <span>${s.name}</span>
      </div>
      <div class="page-header">
        <div>
          <h1 class="page-title">${s.name}</h1>
          <p class="page-subtitle">${s.id}</p>
        </div>
        <div class="inline-actions">
          <span class="badge ${s.valid ? "success" : "error"}">${s.valid ? "Valid" : "Invalid"}</span>
          <button class="btn btn-secondary" data-route="services">Back</button>
        </div>
      </div>

      <div class="tabs">
        <button class="tab-btn ${tab === "overview" ? "active" : ""}" data-action="service-tab" data-tab="overview">Overview</button>
        <button class="tab-btn ${tab === "commands" ? "active" : ""}" data-action="service-tab" data-tab="commands">Commands</button>
        <button class="tab-btn ${tab === "schema" ? "active" : ""}" data-action="service-tab" data-tab="schema">Configuration Schema</button>
        <button class="tab-btn ${tab === "projects" ? "active" : ""}" data-action="service-tab" data-tab="projects">Projects Using</button>
      </div>

      ${panel}
    </section>
  `;
}

function renderProjects() {
  const cards = DATA.projects.map((p) => {
    return `
      <article class="card project-card">
        <div class="card-header">
          <div>
            <div class="item-title">${p.name}</div>
            <div class="item-sub"><code>${p.id}</code></div>
          </div>
          <span class="badge ${p.valid ? "success" : "error"}">${p.valid ? "Valid" : "Invalid"}</span>
        </div>
        <div class="card-body">
          <div class="info-list">
            <div class="info-line"><div class="info-key">Service</div><div class="info-val">${p.serviceName}</div></div>
            <div class="info-line"><div class="info-key">Instances</div><div class="info-val">${p.instanceCount} running</div></div>
            <div class="info-line"><div class="info-key">Auto Start</div><div class="info-val">${p.autoStart ? "Enabled" : "Disabled"}</div></div>
            <div class="info-line"><div class="info-key">Schedule</div><div class="info-val">${p.scheduleType}</div></div>
          </div>
        </div>
        <div class="card-footer">
          <div class="actions">
            <button class="icon-btn" title="Start" ${p.instanceCount > 0 ? "disabled" : ""}>▶</button>
            <button class="icon-btn" title="Stop" ${p.instanceCount === 0 ? "disabled" : ""}>■</button>
          </div>
          <div class="actions">
            <button class="icon-btn" title="Edit" data-route="project-form">✎</button>
            <button class="icon-btn" title="Delete" data-action="delete-project" data-id="${p.id}" data-name="${p.name}">🗑</button>
          </div>
        </div>
      </article>
    `;
  }).join("");

  const tableRows = DATA.projects.map((p) => {
    return `
      <tr>
        <td>${p.name}</td>
        <td><code>${p.id}</code></td>
        <td>${p.serviceName}</td>
        <td>${p.scheduleType}</td>
        <td>${p.instanceCount}</td>
        <td><span class="badge ${p.valid ? "success" : "error"}">${p.valid ? "Valid" : "Invalid"}</span></td>
      </tr>
    `;
  }).join("");

  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Projects</h1>
          <p class="page-subtitle">${DATA.projects.length} projects</p>
        </div>
        <div class="inline-actions">
          <button class="btn btn-primary" data-route="project-form">Create Project</button>
        </div>
      </div>

      <div class="filter-bar">
        <label class="search-field">
          <span class="search-icon">⌕</span>
          <input type="text" placeholder="Search projects...">
        </label>
        <select>
          <option>All Status</option>
          <option>Valid</option>
          <option>Invalid</option>
        </select>
        <select>
          <option>All Auto Start</option>
          <option>Enabled</option>
          <option>Disabled</option>
        </select>
        <div class="view-toggle">
          <button class="${state.projectView === "card" ? "active" : ""}" data-action="project-view" data-view="card">▦</button>
          <button class="${state.projectView === "table" ? "active" : ""}" data-action="project-view" data-view="table">☰</button>
        </div>
      </div>

      ${state.projectView === "card" ? `<div class="card-grid">${cards}</div>` : `
        <div class="table-wrap">
          <table class="table">
            <thead><tr><th>Name</th><th>ID</th><th>Service</th><th>Schedule</th><th>Running</th><th>Status</th></tr></thead>
            <tbody>${tableRows}</tbody>
          </table>
        </div>
      `}
    </section>
  `;
}

function renderProjectForm() {
  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Create Project</h1>
          <p class="page-subtitle">Schema-driven form demo with runtime settings</p>
        </div>
        <div class="inline-actions">
          <button class="btn btn-secondary" data-route="projects">Cancel</button>
          <button class="btn btn-primary" data-action="save-project">Save Project</button>
        </div>
      </div>

      <form class="project-form" onsubmit="return false">
        <section class="form-section">
          <h3>Basic Information</h3>
          <div class="form-grid">
            <label class="field">
              <span>Project ID *</span>
              <input type="text" value="demo-project" aria-label="Project ID">
              <div class="form-tip">Unique identifier, cannot be changed after creation.</div>
            </label>
            <label class="field">
              <span>Name *</span>
              <input type="text" value="Demo Project" aria-label="Name">
            </label>
            <label class="field full">
              <span>Description</span>
              <textarea aria-label="Description">Prototype project from design spec.</textarea>
            </label>
          </div>
        </section>

        <section class="form-section">
          <h3>Service Configuration</h3>
          <div class="form-grid">
            <label class="field">
              <span>Service *</span>
              <select aria-label="Service">
                ${DATA.services.map((s) => `<option value="${s.id}">${s.name}</option>`).join("")}
              </select>
            </label>
            <label class="field">
              <span>Endpoint</span>
              <input type="text" value="tcp://127.0.0.1:502">
            </label>
            <label class="field">
              <span>Timeout (ms)</span>
              <input type="number" value="5000">
            </label>
            <label class="field">
              <span>Retry Count</span>
              <input type="number" value="3">
            </label>
          </div>
        </section>

        <section class="form-section">
          <h3>Runtime Settings</h3>
          <div class="form-grid">
            <label class="switch full">
              <input type="checkbox" checked>
              <span>Auto start on server startup</span>
            </label>
            <div class="field full">
              <span>Restart Policy</span>
              <div class="radio-group">
                <label class="radio"><input type="radio" name="restart" checked> <span>Never</span></label>
                <label class="radio"><input type="radio" name="restart"> <span>On failure</span></label>
                <label class="radio"><input type="radio" name="restart"> <span>Always</span></label>
              </div>
            </div>
            <label class="field">
              <span>Max Consecutive Failures</span>
              <input type="number" value="0">
              <div class="form-tip">0 means unlimited retries.</div>
            </label>
          </div>
        </section>

        <div class="sticky-footer">
          <button class="btn btn-secondary" data-route="projects">Cancel</button>
          <button class="btn btn-primary" data-action="save-project">Save Project</button>
        </div>
      </form>
    </section>
  `;
}

function renderInstances() {
  if (!DATA.instances.length) {
    return `
      <section>
        <div class="page-header"><div><h1 class="page-title">Instances</h1></div></div>
        <div class="empty-state">
          <div class="glyph">▶</div>
          <h3>No running instances</h3>
          <p>Start a project to see active instances.</p>
        </div>
      </section>
    `;
  }

  const rows = DATA.instances.map((i) => {
    return `
      <article class="instance-card">
        <div class="instance-left">
          <span class="instance-pulse ${i.status}"></span>
          <div>
            <div class="item-title">${i.projectName}</div>
            <div class="item-sub"><code>${i.id}</code> / PID ${i.pid || "-"} / ${i.serviceName}</div>
          </div>
        </div>
        <div class="instance-stats">
          <div class="block"><strong>${i.uptime}</strong><span>Uptime</span></div>
          <div class="block"><strong>${i.cpu}</strong><span>CPU</span></div>
          <div class="block"><strong>${i.mem}</strong><span>Memory</span></div>
        </div>
        <div class="instance-actions">
          <button class="icon-btn" title="Logs" data-action="open-logs" data-id="${i.id}">📄</button>
          <button class="icon-btn" title="Terminate">✕</button>
        </div>
      </article>
    `;
  }).join("");

  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Running Instances</h1>
          <p class="page-subtitle">${DATA.instances.filter((i) => i.status === "running").length} active instances</p>
        </div>
        <button class="btn btn-secondary" data-action="refresh">Refresh</button>
      </div>
      <div class="instances-list">${rows}</div>
    </section>
  `;
}

function renderDrivers() {
  const rows = DATA.drivers.map((d) => {
    return `
      <tr>
        <td>${d.name}</td>
        <td><code>${d.path}</code></td>
        <td>v${d.version}</td>
        <td>${d.commands}</td>
        <td><span class="badge ${d.status === "available" ? "success" : "error"}">${d.status}</span></td>
        <td><button class="btn btn-ghost small">Detail</button></td>
      </tr>
    `;
  }).join("");

  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Drivers</h1>
          <p class="page-subtitle">${DATA.drivers.length} registered drivers</p>
        </div>
        <button class="btn btn-secondary" data-action="refresh">Scan Drivers</button>
      </div>

      <div class="table-wrap">
        <table class="table">
          <thead><tr><th>Name</th><th>Path</th><th>Version</th><th>Commands</th><th>Status</th><th></th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>
    </section>
  `;
}

function renderSettings() {
  return `
    <section>
      <div class="page-header">
        <div>
          <h1 class="page-title">Settings</h1>
          <p class="page-subtitle">Server and runtime options</p>
        </div>
      </div>

      <article class="card">
        <div class="card-header"><h3 class="section-title">Server</h3></div>
        <div class="card-body">
          <div class="form-grid">
            <label class="field"><span>Listen Address</span><input value="0.0.0.0" readonly></label>
            <label class="field"><span>Port</span><input value="8080" readonly></label>
            <label class="field full"><span>Data Root</span><input value="./data_root" readonly></label>
            <label class="field"><span>Log Level</span><select><option>info</option><option>debug</option><option>warn</option><option>error</option></select></label>
          </div>
        </div>
      </article>

      <article class="card" style="margin-top:24px">
        <div class="card-header"><h3 class="section-title">About</h3></div>
        <div class="card-body">
          <div class="item-sub">StdioLink Server v1.1.0</div>
          <div class="item-sub">JSONL protocol over stdin/stdout</div>
          <div class="item-sub">Prototype UI generated for design validation</div>
        </div>
      </article>
    </section>
  `;
}

function renderPage() {
  const container = document.getElementById("pageContainer");
  if (!container) return;

  if (state.loading) {
    container.innerHTML = renderLoading();
    return;
  }

  switch (state.page) {
    case "dashboard":
      container.innerHTML = renderDashboard();
      break;
    case "projects":
      container.innerHTML = renderProjects();
      break;
    case "services":
      container.innerHTML = renderServices();
      break;
    case "service-detail":
      container.innerHTML = renderServiceDetail();
      break;
    case "project-form":
      container.innerHTML = renderProjectForm();
      break;
    case "instances":
      container.innerHTML = renderInstances();
      break;
    case "drivers":
      container.innerHTML = renderDrivers();
      break;
    default:
      container.innerHTML = renderSettings();
      break;
  }
}

function syncNav() {
  document.querySelectorAll(".nav-item[data-page]").forEach((el) => {
    el.classList.toggle("active", el.dataset.page === state.page);
  });
  document.querySelectorAll(".bn-item[data-page]").forEach((el) => {
    el.classList.toggle("active", el.dataset.page === state.page);
  });
  const subtitle = document.getElementById("headerSubtitle");
  if (subtitle) subtitle.textContent = pageMeta[state.page] || "StdioLink";
}

function setPage(page) {
  state.page = page;
  renderPage();
  syncNav();
}

function startRefresh() {
  if (state.loading) return;
  state.loading = true;
  const progress = document.getElementById("topProgress");
  progress.classList.add("active");
  renderPage();

  window.setTimeout(() => {
    state.loading = false;
    progress.classList.remove("active");
    renderPage();
    toast("Data refreshed (prototype)", "success");
  }, 900);
}

function openModal(id, show) {
  const modal = document.getElementById(id);
  if (!modal) return;
  modal.classList.toggle("active", show);
  modal.setAttribute("aria-hidden", show ? "false" : "true");
}

function openLogs(instanceId) {
  state.logs.instanceId = instanceId;
  state.logs.level = "all";
  state.logs.search = "";
  const inst = DATA.instances.find((i) => i.id === instanceId);
  document.getElementById("logsTitle").textContent = `Instance Logs - ${instanceId}`;
  document.getElementById("logsSub").textContent = inst ? `${inst.projectName} / ${inst.serviceName}` : "-";
  document.getElementById("logLevelSelect").value = "all";
  document.getElementById("logSearchInput").value = "";
  renderLogs();
  openModal("logsModal", true);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function renderLogs() {
  const root = document.getElementById("logViewer");
  if (!root || !state.logs.instanceId) return;

  const all = DATA.logs[state.logs.instanceId] || [];
  const filtered = all.filter((line) => {
    const passLevel = state.logs.level === "all" || line.level === state.logs.level;
    const passSearch = !state.logs.search || line.msg.toLowerCase().includes(state.logs.search.toLowerCase());
    return passLevel && passSearch;
  });

  const html = filtered.map((line) => {
    let text = escapeHtml(line.msg);
    if (state.logs.search) {
      const escapedSearch = escapeHtml(state.logs.search);
      const reg = new RegExp(escapedSearch.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "ig");
      text = text.replace(reg, (m) => `<mark>${m}</mark>`);
    }
    return `<div class="log-line ${line.level}">${text}</div>`;
  }).join("");

  root.innerHTML = html || '<div class="log-line info">No logs matched current filters.</div>';
  if (state.logs.autoScroll) {
    root.scrollTop = root.scrollHeight;
  }
}

function openConfirm(title, text, onConfirm) {
  document.getElementById("confirmTitle").textContent = title;
  document.getElementById("confirmText").textContent = text;
  confirmAction = onConfirm;
  openModal("confirmModal", true);
}

function toast(message, type = "success") {
  const root = document.getElementById("toastRoot");
  if (!root) return;
  const el = document.createElement("div");
  el.className = `toast ${type}`;
  el.textContent = message;
  root.appendChild(el);
  setTimeout(() => {
    el.remove();
  }, 2800);
}

function closeTopModal() {
  const order = ["logsModal", "commandModal", "shortcutModal", "confirmModal"];
  for (const id of order) {
    const modal = document.getElementById(id);
    if (modal && modal.classList.contains("active")) {
      openModal(id, false);
      return true;
    }
  }
  return false;
}

function getCommandItems() {
  return [
    { key: "go dashboard", label: "Go: Dashboard", run: () => setPage("dashboard") },
    { key: "go projects", label: "Go: Projects", run: () => setPage("projects") },
    { key: "go services", label: "Go: Services", run: () => setPage("services") },
    { key: "go instances", label: "Go: Instances", run: () => setPage("instances") },
    { key: "go drivers", label: "Go: Drivers", run: () => setPage("drivers") },
    { key: "open logs", label: "Open Logs: inst-001", run: () => openLogs("inst-001") },
    { key: "refresh", label: "Refresh current page", run: () => startRefresh() },
    { key: "scan services", label: "Scan services (prototype)", run: () => toast("Service scan simulated", "success") }
  ];
}

function renderCommandList(keyword = "") {
  const list = document.getElementById("commandList");
  if (!list) return;
  const k = keyword.trim().toLowerCase();
  const items = getCommandItems().filter((item) => !k || item.key.includes(k) || item.label.toLowerCase().includes(k));
  list.innerHTML = items.map((item, idx) => {
    return `<button class="command-item" data-action="run-command" data-index="${idx}">${item.label}</button>`;
  }).join("") || "<div class='item-sub' style='padding:8px'>No matching command.</div>";

  list.dataset.cmdPool = JSON.stringify(items.map((i) => i.key));
  list._cmdItems = items;
}

function setupEvents() {
  const shell = document.getElementById("appShell");
  const sideNav = document.getElementById("sideNav");
  const bottomNav = document.getElementById("bottomNav");
  const pageContainer = document.getElementById("pageContainer");

  document.getElementById("menuToggle").addEventListener("click", () => {
    shell.classList.toggle("sidebar-collapsed");
  });

  document.getElementById("shortcutBtn").addEventListener("click", () => {
    openModal("shortcutModal", true);
  });

  sideNav.addEventListener("click", (event) => {
    const btn = event.target.closest(".nav-item[data-page]");
    if (!btn) return;
    setPage(btn.dataset.page);
  });

  bottomNav.addEventListener("click", (event) => {
    const btn = event.target.closest(".bn-item[data-page]");
    if (!btn) return;
    setPage(btn.dataset.page);
  });

  pageContainer.addEventListener("click", (event) => {
    const actionEl = event.target.closest("[data-action]");
    const routeEl = event.target.closest("[data-route]");

    if (routeEl) {
      setPage(routeEl.dataset.route);
      return;
    }

    if (!actionEl) return;

    const action = actionEl.dataset.action;
    if (action === "refresh") {
      startRefresh();
    } else if (action === "open-service") {
      state.selectedService = actionEl.dataset.id;
      state.serviceTab = "overview";
      setPage("service-detail");
    } else if (action === "service-tab") {
      state.serviceTab = actionEl.dataset.tab;
      renderPage();
    } else if (action === "project-view") {
      state.projectView = actionEl.dataset.view;
      renderPage();
    } else if (action === "delete-project") {
      const name = actionEl.dataset.name;
      openConfirm("Delete Project", `Are you sure you want to delete \"${name}\"? This action cannot be undone.`, () => {
        toast(`Deleted ${name} (prototype only)`, "error");
      });
    } else if (action === "open-logs") {
      openLogs(actionEl.dataset.id);
    } else if (action === "save-project") {
      toast("Project saved (prototype only)", "success");
    } else if (action === "run-command") {
      const pool = document.getElementById("commandList")._cmdItems || [];
      const cmd = pool[Number(actionEl.dataset.index)];
      if (cmd) {
        cmd.run();
        openModal("commandModal", false);
      }
    }
  });

  document.querySelectorAll("[data-close]").forEach((el) => {
    el.addEventListener("click", () => {
      const key = el.dataset.close;
      if (key === "logs") openModal("logsModal", false);
      if (key === "shortcut") openModal("shortcutModal", false);
      if (key === "command") openModal("commandModal", false);
      if (key === "confirm") openModal("confirmModal", false);
    });
  });

  document.getElementById("confirmActionBtn").addEventListener("click", () => {
    openModal("confirmModal", false);
    if (typeof confirmAction === "function") {
      confirmAction();
    }
    confirmAction = null;
  });

  document.getElementById("logsRefreshBtn").addEventListener("click", () => {
    toast("Logs refreshed", "success");
    renderLogs();
  });

  document.getElementById("logLevelSelect").addEventListener("change", (event) => {
    state.logs.level = event.target.value;
    renderLogs();
  });

  document.getElementById("logSearchInput").addEventListener("input", (event) => {
    state.logs.search = event.target.value;
    renderLogs();
  });

  document.getElementById("autoScrollToggle").addEventListener("change", (event) => {
    state.logs.autoScroll = event.target.checked;
  });

  const commandInput = document.getElementById("commandInput");
  commandInput.addEventListener("input", (event) => {
    renderCommandList(event.target.value);
  });

  commandInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      const first = document.querySelector(".command-item");
      if (first) first.click();
    }
  });

  const globalSearch = document.getElementById("globalSearch");
  globalSearch.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      toast(`Search executed: ${globalSearch.value || "(empty)"}`, "success");
    }
  });

  window.addEventListener("keydown", (event) => {
    const activeTag = document.activeElement ? document.activeElement.tagName.toLowerCase() : "";
    const inInput = activeTag === "input" || activeTag === "textarea" || activeTag === "select";

    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "k") {
      event.preventDefault();
      openModal("commandModal", true);
      renderCommandList("");
      commandInput.value = "";
      commandInput.focus();
      return;
    }

    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "r") {
      event.preventDefault();
      startRefresh();
      return;
    }

    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "s") {
      if (state.page === "project-form") {
        event.preventDefault();
        toast("Project form saved via shortcut", "success");
      }
      return;
    }

    if (event.key === "Escape") {
      const closed = closeTopModal();
      if (!closed && state.logs.search) {
        state.logs.search = "";
        const input = document.getElementById("logSearchInput");
        if (input) input.value = "";
        renderLogs();
      }
      return;
    }

    if (!inInput && event.key === "/") {
      event.preventDefault();
      globalSearch.focus();
      globalSearch.select();
      return;
    }

    if (!inInput && event.key === "?") {
      event.preventDefault();
      openModal("shortcutModal", true);
    }
  });
}

function init() {
  renderPage();
  syncNav();
  setupEvents();
  renderCommandList("");
  toast("Prototype loaded: static UI only, no backend connection", "success");
}

document.addEventListener("DOMContentLoaded", init);
