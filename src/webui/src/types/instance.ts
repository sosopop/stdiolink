export interface Instance {
  id: string;
  projectId: string;
  serviceId: string;
  pid: number;
  startedAt: string;
  status: string;
  workingDirectory?: string;
  logPath?: string;
  commandLine?: string[];
}

export interface ProcessTreeNode {
  pid: number;
  name: string;
  commandLine: string;
  status: string;
  startedAt?: string;
  resources: ProcessResources;
  children: ProcessTreeNode[];
}

export interface ProcessResources {
  cpuPercent: number;
  memoryRssBytes: number;
  memoryVmsBytes?: number;
  threadCount: number;
  uptimeSeconds?: number;
  ioReadBytes?: number;
  ioWriteBytes?: number;
}

export interface ProcessTreeResponse {
  instanceId: string;
  rootPid: number;
  tree: ProcessTreeNode;
  summary: ProcessTreeSummary;
}

export interface ProcessTreeSummary {
  totalProcesses: number;
  totalCpuPercent: number;
  totalMemoryRssBytes: number;
  totalThreads: number;
}

export interface ResourcesResponse {
  instanceId: string;
  timestamp: string;
  processes: ProcessInfo[];
  summary: ProcessTreeSummary;
}

export interface ProcessInfo {
  pid: number;
  name: string;
  cpuPercent: number;
  memoryRssBytes: number;
  threadCount: number;
  uptimeSeconds: number;
  ioReadBytes: number;
  ioWriteBytes: number;
}
