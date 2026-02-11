// ============================================
// StdioLink UI Prototype - Mock Data & Renderer
// ============================================

// --- Mock Data ---
const MOCK = {
  metrics: {
    serviceCount: 12, validServiceCount: 10,
    projectCount: 8, projectTrend: '+2',
    runningInstanceCount: 15, totalInstanceCount: 23,
    failuresToday: 2,
    running: 15, stopped: 5, errored: 2, pending: 1,
    daemonCount: 4, daemonActiveCount: 3,
    fixedRateCount: 3, nextScheduledRun: '14:30',
    manualCount: 5, lastManualRun: '2h ago'
  },

  projects: [
    { id: 'proj-001', name: 'Vision Pipeline', serviceId: 'svc-3dvision', serviceName: '3D Vision Service', valid: true, autoStart: true, scheduleType: 'daemon', instanceCount: 3 },
    { id: 'proj-002', name: 'Modbus RTU Poller', serviceId: 'svc-modbusrtu', serviceName: 'Modbus RTU Service', valid: true, autoStart: true, scheduleType: 'fixed_rate', instanceCount: 1 },
    { id: 'proj-003', name: 'TCP Gateway', serviceId: 'svc-modbustcp', serviceName: 'Modbus TCP Service', valid: true, autoStart: false, scheduleType: 'manual', instanceCount: 0 },
    { id: 'proj-004', name: 'Data Aggregator', serviceId: 'svc-aggregator', serviceName: 'Aggregator Service', valid: false, autoStart: false, scheduleType: 'manual', instanceCount: 0 },
    { id: 'proj-005', name: 'Alert Monitor', serviceId: 'svc-alert', serviceName: 'Alert Service', valid: true, autoStart: true, scheduleType: 'daemon', instanceCount: 2 },
    { id: 'proj-006', name: 'Log Collector', serviceId: 'svc-logcol', serviceName: 'Log Collector Service', valid: true, autoStart: false, scheduleType: 'fixed_rate', instanceCount: 1 },
  ],

  services: [
    { id: 'svc-3dvision', name: '3D Vision Service', version: '2.1.0', description: 'Industrial 3D vision processing pipeline', commands: 5, drivers: 2, projects: 2 },
    { id: 'svc-modbusrtu', name: 'Modbus RTU Service', version: '1.4.2', description: 'Serial Modbus RTU communication service', commands: 3, drivers: 1, projects: 1 },
    { id: 'svc-modbustcp', name: 'Modbus TCP Service', version: '1.4.2', description: 'TCP-based Modbus communication service', commands: 3, drivers: 1, projects: 1 },
    { id: 'svc-aggregator', name: 'Aggregator Service', version: '0.9.0', description: 'Multi-source data aggregation and transform', commands: 4, drivers: 3, projects: 1 },
    { id: 'svc-alert', name: 'Alert Service', version: '1.2.0', description: 'Threshold monitoring and alert notification', commands: 2, drivers: 1, projects: 1 },
    { id: 'svc-logcol', name: 'Log Collector Service', version: '1.0.1', description: 'Centralized log collection and forwarding', commands: 2, drivers: 1, projects: 1 },
  ],

  instances: [
    { id: 'inst-001', projectName: 'Vision Pipeline', pid: 12847, status: 'running', uptime: '2d 14h 32m', cpu: '12%', mem: '256 MB' },
    { id: 'inst-002', projectName: 'Vision Pipeline', pid: 12848, status: 'running', uptime: '2d 14h 32m', cpu: '8%', mem: '198 MB' },
    { id: 'inst-003', projectName: 'Vision Pipeline', pid: 12849, status: 'running', uptime: '1d 6h 15m', cpu: '15%', mem: '312 MB' },
    { id: 'inst-004', projectName: 'Modbus RTU Poller', pid: 13201, status: 'running', uptime: '5d 2h 10m', cpu: '3%', mem: '64 MB' },
    { id: 'inst-005', projectName: 'Alert Monitor', pid: 13450, status: 'running', uptime: '3d 8h 45m', cpu: '5%', mem: '128 MB' },
    { id: 'inst-006', projectName: 'Alert Monitor', pid: 13451, status: 'error', uptime: '0h 2m', cpu: '0%', mem: '32 MB' },
    { id: 'inst-007', projectName: 'Log Collector', pid: 14002, status: 'running', uptime: '12h 30m', cpu: '2%', mem: '48 MB' },
    { id: 'inst-008', projectName: 'TCP Gateway', pid: 0, status: 'stopped', uptime: '-', cpu: '-', mem: '-' },
  ],

  drivers: [
    { id: 'drv-3dvision', name: 'driver_3dvision', path: 'bin/driver_3dvision', version: '2.1.0', commands: 5, status: 'available' },
    { id: 'drv-modbusrtu', name: 'driver_modbusrtu', path: 'bin/driver_modbusrtu', version: '1.4.2', commands: 3, status: 'available' },
    { id: 'drv-modbustcp', name: 'driver_modbustcp', path: 'bin/driver_modbustcp', version: '1.4.2', commands: 3, status: 'available' },
    { id: 'drv-aggregator', name: 'driver_aggregator', path: 'bin/driver_aggregator', version: '0.9.0', commands: 4, status: 'unavailable' },
    { id: 'drv-alert', name: 'driver_alert', path: 'bin/driver_alert', version: '1.2.0', commands: 2, status: 'available' },
  ],

  events: [
    { type: 'instanceStarted', message: 'Vision Pipeline instance started (PID 12849)', time: '5 min ago' },
    { type: 'instanceFailed', message: 'Alert Monitor instance crashed (exit code 139)', time: '12 min ago' },
    { type: 'scheduleTriggered', message: 'Log Collector scheduled execution triggered', time: '30 min ago' },
    { type: 'projectValidated', message: 'TCP Gateway project validated successfully', time: '1h ago' },
    { type: 'serviceScanned', message: 'Service scan completed: 12 services found', time: '2h ago' },
    { type: 'instanceStopped', message: 'TCP Gateway instance stopped gracefully', time: '3h ago' },
    { type: 'projectCreated', message: 'New project "Log Collector" created', time: '5h ago' },
    { type: 'instanceStarted', message: 'Modbus RTU Poller instance restarted', time: '6h ago' },
  ]
};

// --- SVG Helpers ---
function createDonutSVG(data, size, inner) {
  const cx = size/2, cy = size/2, r = (size - 8) / 2;
  const total = data.reduce((s, d) => s + d.value, 0);
  let cumulative = 0;
  let paths = '';
  data.forEach(d => {
    if (d.value === 0) return;
    const start = cumulative / total;
    const end = (cumulative + d.value) / total;
    cumulative += d.value;
    const s1 = start * Math.PI * 2 - Math.PI/2;
    const s2 = end * Math.PI * 2 - Math.PI/2;
    const large = (end - start) > 0.5 ? 1 : 0;
    const x1 = cx + r * Math.cos(s1), y1 = cy + r * Math.sin(s1);
    const x2 = cx + r * Math.cos(s2), y2 = cy + r * Math.sin(s2);
    const ir = inner;
    const x3 = cx + ir * Math.cos(s2), y3 = cy + ir * Math.sin(s2);
    const x4 = cx + ir * Math.cos(s1), y4 = cy + ir * Math.sin(s1);
    paths += `<path d="M${x1},${y1} A${r},${r} 0 ${large} 1 ${x2},${y2} L${x3},${y3} A${ir},${ir} 0 ${large} 0 ${x4},${y4} Z" fill="${d.color}"/>`;
  });
  return `<svg width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">${paths}</svg>`;
}

function createAreaSVG(w, h) {
  // Simulated area chart with random-ish data
  const pts = 24;
  const lines = [];
  const colors = [
    { stroke: '#4CAF50', fill: 'rgba(76,175,80,0.15)' },
    { stroke: '#2196F3', fill: 'rgba(33,150,243,0.08)' },
    { stroke: '#F44336', fill: 'rgba(244,67,54,0.08)' }
  ];
  const datasets = [
    [12,14,13,15,14,16,15,17,16,15,14,15,16,18,17,16,15,14,15,16,17,15,14,15],
    [2,3,1,2,3,2,1,3,2,1,2,3,2,4,3,2,1,2,3,2,1,2,3,2],
    [0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0]
  ];
  const maxVal = 20;
  const padX = 40, padY = 20;
  const cw = w - padX * 2, ch = h - padY * 2;

  // Grid lines
  let grid = '';
  for (let i = 0; i <= 4; i++) {
    const y = padY + (ch / 4) * i;
    grid += `<line x1="${padX}" y1="${y}" x2="${w-padX}" y2="${y}" stroke="#EEEEEE" stroke-width="1"/>`;
    grid += `<text x="${padX-8}" y="${y+4}" text-anchor="end" fill="#9E9E9E" font-size="10">${maxVal - (maxVal/4)*i}</text>`;
  }
  // X labels
  for (let i = 0; i < pts; i += 4) {
    const x = padX + (cw / (pts-1)) * i;
    grid += `<text x="${x}" y="${h-4}" text-anchor="middle" fill="#9E9E9E" font-size="10">${String(i).padStart(2,'0')}:00</text>`;
  }

  let areas = '';
  datasets.forEach((data, di) => {
    const points = data.map((v, i) => {
      const x = padX + (cw / (pts-1)) * i;
      const y = padY + ch - (v / maxVal) * ch;
      return `${x},${y}`;
    });
    const baseline = `${padX + cw},${padY + ch} ${padX},${padY + ch}`;
    areas += `<polygon points="${points.join(' ')} ${baseline}" fill="${colors[di].fill}"/>`;
    areas += `<polyline points="${points.join(' ')}" fill="none" stroke="${colors[di].stroke}" stroke-width="2"/>`;
  });

  return `<svg width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" style="font-family:Inter,sans-serif">${grid}${areas}</svg>`;
}

// --- Page Renderers ---

function renderDashboard() {
  const m = MOCK.metrics;
  const eventIconMap = {
    instanceStarted: { cls: 'green', icon: 'lucide-play' },
    instanceStopped: { cls: 'gray', icon: 'lucide-square' },
    instanceFailed:  { cls: 'red', icon: 'lucide-x-circle' },
    projectCreated:  { cls: 'blue', icon: 'lucide-plus' },
    projectValidated:{ cls: 'green', icon: 'lucide-check-circle' },
    serviceScanned:  { cls: 'blue', icon: 'lucide-refresh-cw' },
    scheduleTriggered:{ cls: 'orange', icon: 'lucide-clock' }
  };

  const donutData = [
    { label: 'Running', value: m.running, color: '#4CAF50' },
    { label: 'Stopped', value: m.stopped, color: '#BDBDBD' },
    { label: 'Error',   value: m.errored, color: '#F44336' },
    { label: 'Pending', value: m.pending, color: '#FF9800' }
  ];

  return `
  <div class="page active" id="page-dashboard">
    <div class="page-header">
      <div>
        <h1>Dashboard</h1>
        <div class="subtitle">System overview at a glance</div>
      </div>
      <button class="btn btn-secondary"><i class="lucide lucide-refresh-cw"></i> Refresh</button>
    </div>

    <!-- Metric Cards -->
    <div class="grid grid-4">
      <div class="metric-card">
        <div class="metric-icon blue"><i class="lucide lucide-package"></i></div>
        <div class="metric-info">
          <div class="metric-label">Total Services</div>
          <div class="metric-value">${m.serviceCount}</div>
          <div class="metric-desc">${m.validServiceCount} valid</div>
        </div>
      </div>
      <div class="metric-card">
        <div class="metric-icon blue"><i class="lucide lucide-folder-open"></i></div>
        <div class="metric-info">
          <div class="metric-label">Active Projects</div>
          <div class="metric-value">${m.projectCount}</div>
          <div class="metric-desc">${m.projectTrend} vs last week</div>
        </div>
      </div>
      <div class="metric-card">
        <div class="metric-icon green"><i class="lucide lucide-play-circle"></i></div>
        <div class="metric-info">
          <div class="metric-label">Running Instances</div>
          <div class="metric-value">${m.runningInstanceCount}</div>
          <div class="metric-desc">of ${m.totalInstanceCount} total</div>
        </div>
      </div>
      <div class="metric-card">
        <div class="metric-icon ${m.failuresToday > 0 ? 'red' : 'green'}"><i class="lucide lucide-alert-triangle"></i></div>
        <div class="metric-info">
          <div class="metric-label">Failures Today</div>
          <div class="metric-value">${m.failuresToday}</div>
          <div class="metric-desc">auto-restart triggered</div>
        </div>
      </div>
    </div>

    <!-- Middle: Donut + Area Chart -->
    <div class="grid grid-12 mt-6">
      <div class="col-4">
        <div class="card">
          <div class="card-header"><h3>Instance Status</h3></div>
          <div class="card-body">
            <div class="donut-container">
              <div class="donut-chart" style="position:relative">
                ${createDonutSVG(donutData, 180, 60)}
                <div class="donut-center">
                  <div class="num">${m.totalInstanceCount}</div>
                  <div class="label">Total</div>
                </div>
              </div>
              <div class="donut-legend">
                ${donutData.map(d => `<div class="legend-item"><span class="legend-dot" style="background:${d.color}"></span>${d.label}: ${d.value}</div>`).join('')}
              </div>
            </div>
          </div>
        </div>
      </div>
      <div class="col-8">
        <div class="card">
          <div class="card-header">
            <h3>Instance Activity</h3>
            <select class="btn-sm" style="border:1px solid #E0E0E0;border-radius:4px;padding:4px 8px;font-size:13px">
              <option>Last 24 hours</option><option>Last 7 days</option><option>Last 1 hour</option>
            </select>
          </div>
          <div class="card-body" style="padding:12px 16px">
            ${createAreaSVG(640, 240)}
            <div class="flex gap-4" style="justify-content:center;gap:20px;margin-top:8px">
              <div class="legend-item"><span class="legend-dot" style="background:#4CAF50"></span>Running</div>
              <div class="legend-item"><span class="legend-dot" style="background:#2196F3"></span>Started</div>
              <div class="legend-item"><span class="legend-dot" style="background:#F44336"></span>Failed</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Bottom: Health + Events -->
    <div class="grid grid-12 mt-6">
      <div class="col-7">
        <div class="card">
          <div class="card-header">
            <h3>Project Health</h3>
            <button class="btn btn-ghost btn-sm" onclick="navigateTo('projects')">View All</button>
          </div>
          <div class="card-body" style="padding:0">
            ${MOCK.projects.slice(0, 5).map(p => `
              <div class="health-item">
                <div class="health-left">
                  <span class="health-dot ${p.valid ? (p.instanceCount > 0 ? 'ok' : 'warn') : 'err'}"></span>
                  <div>
                    <div class="health-name">${p.name}</div>
                    <div class="health-svc">${p.serviceName}</div>
                  </div>
                </div>
                <div class="health-right">
                  <span class="health-count">${p.instanceCount} instance${p.instanceCount !== 1 ? 's' : ''}</span>
                  <span class="badge ${p.valid ? 'badge-success' : 'badge-error'}">${p.valid ? 'Healthy' : 'Invalid'}</span>
                </div>
              </div>
            `).join('')}
          </div>
        </div>
      </div>
      <div class="col-5">
        <div class="card">
          <div class="card-header">
            <h3>Recent Events</h3>
            <span class="badge badge-info">${MOCK.events.length} new</span>
          </div>
          <div class="card-body" style="padding:0">
            <ul class="event-list">
              ${MOCK.events.map(e => {
                const ei = eventIconMap[e.type] || { cls: 'gray', icon: 'lucide-info' };
                return `<li class="event-item">
                  <div class="event-icon ${ei.cls}"><i class="lucide ${ei.icon}"></i></div>
                  <div>
                    <div class="event-text">${e.message}</div>
                    <div class="event-time">${e.time}</div>
                  </div>
                </li>`;
              }).join('')}
            </ul>
          </div>
        </div>
      </div>
    </div>

    <!-- Schedule Overview -->
    <div class="card mt-6">
      <div class="card-header"><h3>Schedule Overview</h3></div>
      <div class="card-body">
        <div class="grid grid-3">
          <div class="schedule-card">
            <div class="schedule-icon daemon"><i class="lucide lucide-shield"></i></div>
            <div class="schedule-count">${m.daemonCount}</div>
            <div class="schedule-label">Daemon Projects</div>
            <div class="schedule-desc">${m.daemonActiveCount} active &middot; Always-on with auto-restart</div>
          </div>
          <div class="schedule-card">
            <div class="schedule-icon fixed"><i class="lucide lucide-clock"></i></div>
            <div class="schedule-count">${m.fixedRateCount}</div>
            <div class="schedule-label">Scheduled Tasks</div>
            <div class="schedule-desc">Next run: ${m.nextScheduledRun} &middot; Periodic execution</div>
          </div>
          <div class="schedule-card">
            <div class="schedule-icon manual"><i class="lucide lucide-hand"></i></div>
            <div class="schedule-count">${m.manualCount}</div>
            <div class="schedule-label">Manual Projects</div>
            <div class="schedule-desc">Last run: ${m.lastManualRun} &middot; On-demand</div>
          </div>
        </div>
      </div>
    </div>
  </div>`;
}

function renderProjects() {
  return `
  <div class="page" id="page-projects">
    <div class="page-header">
      <div>
        <h1>Projects</h1>
        <div class="subtitle">${MOCK.projects.length} projects</div>
      </div>
      <button class="btn btn-primary"><i class="lucide lucide-plus"></i> Create Project</button>
    </div>

    <div class="filter-bar">
      <div class="search-wrap">
        <i class="lucide lucide-search" style="width:16px;height:16px"></i>
        <input type="text" placeholder="Search projects...">
      </div>
      <select><option>All Status</option><option>Valid</option><option>Invalid</option></select>
      <select><option>All Auto Start</option><option>Enabled</option><option>Disabled</option></select>
      <div class="view-toggle">
        <button class="active"><i class="lucide lucide-layout-grid"></i></button>
        <button><i class="lucide lucide-list"></i></button>
      </div>
    </div>

    <div class="grid grid-3">
      ${MOCK.projects.map(p => `
        <div class="project-card">
          <div class="project-card-header">
            <div class="left">
              <div class="proj-icon"><i class="lucide lucide-folder-open"></i></div>
              <div>
                <div class="proj-name">${p.name}</div>
                <div class="proj-id">${p.id}</div>
              </div>
            </div>
            <span class="badge ${p.valid ? 'badge-success' : 'badge-error'}">${p.valid ? 'Valid' : 'Invalid'}</span>
          </div>
          <div class="project-card-body">
            <div class="info-item">
              <div class="info-label">Service</div>
              <div class="info-value"><a href="#">${p.serviceName}</a></div>
            </div>
            <div class="info-item">
              <div class="info-label">Instances</div>
              <div class="info-value ${p.instanceCount > 0 ? 'text-success' : ''}">${p.instanceCount} running</div>
            </div>
            <div class="info-item">
              <div class="info-label">Auto Start</div>
              <div class="info-value"><span class="badge ${p.autoStart ? 'badge-info' : 'badge-default'}">${p.autoStart ? 'Enabled' : 'Disabled'}</span></div>
            </div>
            <div class="info-item">
              <div class="info-label">Schedule</div>
              <div class="info-value">${p.scheduleType}</div>
            </div>
          </div>
          <div class="project-card-footer">
            <div class="actions">
              <button class="btn-icon" title="Start" ${p.instanceCount > 0 ? 'disabled' : ''}><i class="lucide lucide-play"></i></button>
              <button class="btn-icon" title="Stop" ${p.instanceCount === 0 ? 'disabled' : ''}><i class="lucide lucide-square"></i></button>
            </div>
            <div class="actions">
              <button class="btn-icon" title="Edit"><i class="lucide lucide-edit-2"></i></button>
              <button class="btn-icon danger" title="Delete"><i class="lucide lucide-trash-2"></i></button>
            </div>
          </div>
        </div>
      `).join('')}
    </div>
  </div>`;
}

function renderServices() {
  return `
  <div class="page" id="page-services">
    <div class="page-header">
      <div>
        <h1>Services</h1>
        <div class="subtitle">${MOCK.services.length} services discovered</div>
      </div>
      <button class="btn btn-secondary"><i class="lucide lucide-refresh-cw"></i> Scan</button>
    </div>
    <div class="filter-bar">
      <div class="search-wrap">
        <i class="lucide lucide-search" style="width:16px;height:16px"></i>
        <input type="text" placeholder="Search services...">
      </div>
    </div>
    <div class="grid grid-3">
      ${MOCK.services.map(s => `
        <div class="service-card">
          <div class="service-card-header">
            <div class="svc-icon"><i class="lucide lucide-package"></i></div>
            <div>
              <div class="svc-name">${s.name}</div>
              <div class="svc-version">v${s.version}</div>
            </div>
          </div>
          <div style="font-size:13px;color:#757575;margin-bottom:12px">${s.description}</div>
          <div class="service-card-meta">
            <span class="meta-tag"><i class="lucide lucide-terminal" style="width:12px;height:12px"></i> ${s.commands} cmds</span>
            <span class="meta-tag"><i class="lucide lucide-cpu" style="width:12px;height:12px"></i> ${s.drivers} drivers</span>
            <span class="meta-tag"><i class="lucide lucide-folder-open" style="width:12px;height:12px"></i> ${s.projects} projects</span>
          </div>
        </div>
      `).join('')}
    </div>
  </div>`;
}

function renderInstances() {
  const running = MOCK.instances.filter(i => i.status === 'running').length;
  return `
  <div class="page" id="page-instances">
    <div class="page-header">
      <div>
        <h1>Instances</h1>
        <div class="subtitle">${running} running / ${MOCK.instances.length} total</div>
      </div>
    </div>
    <div class="filter-bar">
      <div class="search-wrap">
        <i class="lucide lucide-search" style="width:16px;height:16px"></i>
        <input type="text" placeholder="Search instances...">
      </div>
      <select><option>All Status</option><option>Running</option><option>Stopped</option><option>Error</option></select>
    </div>
    <div style="display:flex;flex-direction:column;gap:12px">
      ${MOCK.instances.map(inst => `
        <div class="instance-card">
          <div class="instance-status-dot ${inst.status}"></div>
          <div class="instance-info">
            <div class="inst-name">${inst.projectName}</div>
            <div class="inst-detail">PID ${inst.pid} &middot; ${inst.id}</div>
          </div>
          <div class="instance-stats">
            <div class="stat"><div class="stat-val">${inst.uptime}</div><div class="stat-label">Uptime</div></div>
            <div class="stat"><div class="stat-val">${inst.cpu}</div><div class="stat-label">CPU</div></div>
            <div class="stat"><div class="stat-val">${inst.mem}</div><div class="stat-label">Memory</div></div>
          </div>
          <div style="display:flex;gap:4px">
            <button class="btn-icon" title="Logs"><i class="lucide lucide-file-text"></i></button>
            <button class="btn-icon danger" title="Terminate"><i class="lucide lucide-x-circle"></i></button>
          </div>
        </div>
      `).join('')}
    </div>
  </div>`;
}

function renderDrivers() {
  return `
  <div class="page" id="page-drivers">
    <div class="page-header">
      <div>
        <h1>Drivers</h1>
        <div class="subtitle">${MOCK.drivers.length} drivers registered</div>
      </div>
      <button class="btn btn-secondary"><i class="lucide lucide-refresh-cw"></i> Scan</button>
    </div>
    <div class="filter-bar">
      <div class="search-wrap">
        <i class="lucide lucide-search" style="width:16px;height:16px"></i>
        <input type="text" placeholder="Search drivers...">
      </div>
    </div>
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Path</th>
            <th>Version</th>
            <th>Commands</th>
            <th>Status</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          ${MOCK.drivers.map(d => `
            <tr>
              <td style="font-weight:600"><i class="lucide lucide-cpu" style="width:14px;height:14px;vertical-align:-2px;margin-right:6px;color:#757575"></i>${d.name}</td>
              <td><code style="font-size:12px;color:#757575;background:#F5F5F5;padding:2px 6px;border-radius:3px">${d.path}</code></td>
              <td>v${d.version}</td>
              <td style="text-align:center">${d.commands}</td>
              <td><span class="badge ${d.status === 'available' ? 'badge-success' : 'badge-error'}">${d.status}</span></td>
              <td style="text-align:right">
                <button class="btn btn-ghost btn-sm"><i class="lucide lucide-info" style="width:14px;height:14px"></i> Detail</button>
              </td>
            </tr>
          `).join('')}
        </tbody>
      </table>
    </div>
  </div>`;
}

function renderSettings() {
  return `
  <div class="page" id="page-settings">
    <div class="page-header">
      <div>
        <h1>Settings</h1>
        <div class="subtitle">Server configuration</div>
      </div>
    </div>
    <div class="card">
      <div class="card-header"><h3>Server</h3></div>
      <div class="card-body">
        <div style="display:grid;grid-template-columns:160px 1fr;gap:16px;align-items:center">
          <label style="font-size:13px;font-weight:500;color:#616161">Listen Address</label>
          <input type="text" value="0.0.0.0" style="padding:8px 12px;border:1px solid #E0E0E0;border-radius:4px;font-size:14px;width:240px" readonly>
          <label style="font-size:13px;font-weight:500;color:#616161">Port</label>
          <input type="text" value="8080" style="padding:8px 12px;border:1px solid #E0E0E0;border-radius:4px;font-size:14px;width:240px" readonly>
          <label style="font-size:13px;font-weight:500;color:#616161">Data Root</label>
          <code style="font-size:13px;color:#757575;background:#F5F5F5;padding:6px 10px;border-radius:4px">./data_root</code>
          <label style="font-size:13px;font-weight:500;color:#616161">Log Level</label>
          <select style="padding:8px 12px;border:1px solid #E0E0E0;border-radius:4px;font-size:14px;width:240px">
            <option>info</option><option>debug</option><option>warn</option><option>error</option>
          </select>
        </div>
      </div>
    </div>
    <div class="card mt-6">
      <div class="card-header"><h3>About</h3></div>
      <div class="card-body">
        <div style="font-size:13px;color:#757575;line-height:2">
          <div><strong>StdioLink Server</strong> v1.1.0</div>
          <div>Qt-based cross-platform IPC framework</div>
          <div>JSONL protocol over stdin/stdout</div>
        </div>
      </div>
    </div>
  </div>`;
}

// --- Navigation ---
let currentPage = 'dashboard';

function navigateTo(page) {
  currentPage = page;
  document.querySelectorAll('.nav-item').forEach(el => {
    el.classList.toggle('active', el.dataset.page === page);
  });
  document.querySelectorAll('.page').forEach(el => {
    el.classList.toggle('active', el.id === 'page-' + page);
  });
}

// --- Init ---
document.addEventListener('DOMContentLoaded', () => {
  const main = document.getElementById('main-content');
  main.innerHTML =
    renderDashboard() +
    renderProjects() +
    renderServices() +
    renderInstances() +
    renderDrivers() +
    renderSettings();

  document.querySelectorAll('.nav-item[data-page]').forEach(el => {
    el.addEventListener('click', () => navigateTo(el.dataset.page));
  });
});