export const mockServerStatus = {
  status: 'running',
  version: '0.1.0',
  uptimeMs: 3600000,
  startedAt: '2025-01-01T00:00:00Z',
  host: '0.0.0.0',
  port: 8080,
  dataRoot: '/data',
  serviceProgram: 'stdiolink_service',
  counts: {
    services: 3,
    projects: { total: 5, valid: 4, invalid: 1, enabled: 3, disabled: 2 },
    instances: { total: 2, running: 2 },
    drivers: 2,
  },
  system: {
    platform: 'linux',
    cpuCores: 8,
    totalMemoryBytes: 17179869184,
  },
};

export const mockServices = [
  {
    id: 'demo-service',
    name: 'Demo Service',
    version: '1.0.0',
    serviceDir: '/data/services/demo-service',
    hasSchema: true,
    projectCount: 2,
  },
  {
    id: 'modbus-service',
    name: 'Modbus Service',
    version: '2.0.0',
    serviceDir: '/data/services/modbus-service',
    hasSchema: true,
    projectCount: 1,
  },
];

export const mockServiceDetail = {
  id: 'demo-service',
  name: 'Demo Service',
  version: '1.0.0',
  serviceDir: '/data/services/demo-service',
  hasSchema: true,
  projectCount: 2,
  manifest: {
    manifestVersion: '1.0',
    id: 'demo-service',
    name: 'Demo Service',
    version: '1.0.0',
    description: 'A demo service for testing',
    author: 'Test Author',
  },
  configSchema: {
    host: { type: 'string', required: true, description: 'Host address' },
    port: { type: 'int', default: 3306 },
  },
  configSchemaFields: [
    { name: 'host', type: 'string', required: true, description: 'Host address' },
    { name: 'port', type: 'int', default: 3306, description: 'Port number' },
  ],
  projects: ['demo-project'],
};

export const mockProjects = [
  {
    id: 'demo-project',
    name: 'Demo Project',
    serviceId: 'demo-service',
    enabled: true,
    valid: true,
    config: { host: 'localhost', port: 3306 },
    schedule: { type: 'manual' },
    instanceCount: 1,
    status: 'running',
  },
  {
    id: 'test-project',
    name: 'Test Project',
    serviceId: 'modbus-service',
    enabled: false,
    valid: true,
    config: {},
    schedule: { type: 'daemon', restartDelayMs: 5000 },
    instanceCount: 0,
    status: 'stopped',
  },
];

export const mockProjectDetail = {
  ...mockProjects[0],
};

export const mockProjectRuntime = {
  id: 'demo-project',
  enabled: true,
  valid: true,
  status: 'running',
  runningInstances: 1,
  instances: [
    {
      id: 'inst-001',
      projectId: 'demo-project',
      serviceId: 'demo-service',
      pid: 12345,
      startedAt: '2025-01-01T01:00:00Z',
      status: 'running',
    },
  ],
  schedule: {
    type: 'manual',
    timerActive: false,
    restartSuppressed: false,
    consecutiveFailures: 0,
    shuttingDown: false,
    autoRestarting: false,
  },
};

export const mockInstances = [
  {
    id: 'inst-001',
    projectId: 'demo-project',
    serviceId: 'demo-service',
    pid: 12345,
    startedAt: '2025-01-01T01:00:00Z',
    status: 'running',
    workingDirectory: '/data/projects/demo-project',
    logPath: '/data/logs/inst-001.log',
  },
  {
    id: 'inst-002',
    projectId: 'test-project',
    serviceId: 'modbus-service',
    pid: 12346,
    startedAt: '2025-01-01T02:00:00Z',
    status: 'running',
    workingDirectory: '/data/projects/test-project',
    logPath: '/data/logs/inst-002.log',
  },
];

export const mockInstanceDetail = mockInstances[0];

export const mockProcessTree = {
  instanceId: 'inst-001',
  rootPid: 12345,
  tree: {
    pid: 12345,
    name: 'stdiolink_service',
    commandLine: 'stdiolink_service --project demo-project',
    status: 'running',
    resources: {
      cpuPercent: 2.5,
      memoryRssBytes: 52428800,
      threadCount: 4,
      uptimeSeconds: 3600,
      ioReadBytes: 1024000,
      ioWriteBytes: 512000,
    },
    children: [],
  },
  summary: {
    totalProcesses: 1,
    totalCpuPercent: 2.5,
    totalMemoryRssBytes: 52428800,
    totalThreads: 4,
  },
};

export const mockResources = {
  instanceId: 'inst-001',
  timestamp: '2025-01-01T02:00:00Z',
  processes: [
    {
      pid: 12345,
      name: 'stdiolink_service',
      cpuPercent: 2.5,
      memoryRssBytes: 52428800,
      threadCount: 4,
      uptimeSeconds: 3600,
      ioReadBytes: 1024000,
      ioWriteBytes: 512000,
    },
  ],
  summary: {
    totalProcesses: 1,
    totalCpuPercent: 2.5,
    totalMemoryRssBytes: 52428800,
    totalThreads: 4,
  },
};

export const mockInstanceLogs = {
  instanceId: 'inst-001',
  lines: [
    '[2025-01-01 01:00:00] INFO  Service started',
    '[2025-01-01 01:00:01] INFO  Listening on port 3306',
    '[2025-01-01 01:00:05] INFO  Connection established',
  ],
};

export const mockDrivers = [
  {
    id: 'demo-driver',
    program: '/usr/bin/demo-driver',
    metaHash: 'abc123',
    name: 'Demo Driver',
    version: '1.0.0',
  },
  {
    id: 'modbus-driver',
    program: '/usr/bin/modbus-driver',
    metaHash: 'def456',
    name: 'Modbus RTU Driver',
    version: '2.0.0',
  },
];

export const mockDriverDetail = {
  id: 'demo-driver',
  program: '/usr/bin/demo-driver',
  metaHash: 'abc123',
  meta: {
    schemaVersion: '1.0',
    info: {
      name: 'Demo Driver',
      version: '1.0.0',
      description: 'A demo driver for testing',
    },
    commands: [
      {
        name: 'read',
        description: 'Read data from device',
        params: [
          { name: 'address', type: 'string', required: true, description: 'Device address' },
        ],
        returns: { type: 'object', description: 'Read result' },
      },
      {
        name: 'write',
        description: 'Write data to device',
        params: [
          { name: 'address', type: 'string', required: true },
          { name: 'value', type: 'any', required: true },
        ],
        returns: { type: 'bool', description: 'Success' },
      },
    ],
  },
};
