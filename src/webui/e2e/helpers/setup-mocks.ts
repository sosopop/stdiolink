import type { Page } from '@playwright/test';
import {
  mockServerStatus,
  mockServices,
  mockServiceDetail,
  mockProjects,
  mockProjectDetail,
  mockProjectRuntime,
  mockInstances,
  mockInstanceDetail,
  mockProcessTree,
  mockResources,
  mockDrivers,
  mockDriverDetail,
  mockInstanceLogs,
} from '../mocks/api-handlers';

export async function setupApiMocks(page: Page): Promise<void> {
  // Server status
  await page.route('**/api/server/status', (route) =>
    route.fulfill({ json: mockServerStatus }),
  );

  // SSE event stream - 返回空流避免挂起
  await page.route('**/api/events/stream**', (route) =>
    route.fulfill({ status: 200, body: '', contentType: 'text/event-stream' }),
  );

  // Services
  await page.route('**/api/services', (route) => {
    if (route.request().method() === 'GET') {
      return route.fulfill({ json: { services: mockServices } });
    }
    return route.fulfill({ json: { success: true } });
  });

  await page.route('**/api/services/demo-service', (route) =>
    route.fulfill({ json: mockServiceDetail }),
  );

  await page.route('**/api/services/demo-service/files', (route) =>
    route.fulfill({
      json: {
        serviceId: 'demo-service',
        serviceDir: '/data/services/demo-service',
        files: [
          { name: 'index.js', path: 'index.js', size: 1024, type: 'file', modifiedAt: '2025-01-01T00:00:00Z' },
          { name: 'manifest.json', path: 'manifest.json', size: 256, type: 'file', modifiedAt: '2025-01-01T00:00:00Z' },
        ],
      },
    }),
  );

  // Projects（支持 ?serviceId=xxx 等查询参数）
  await page.route((url) => {
    return url.pathname.endsWith('/api/projects');
  }, (route) => {
    if (route.request().method() === 'GET') {
      return route.fulfill({ json: { projects: mockProjects } });
    }
    return route.fulfill({ json: { ...mockProjects[0], created: true } });
  });

  await page.route('**/api/projects/demo-project', (route) => {
    if (route.request().method() === 'GET') {
      return route.fulfill({ json: mockProjectDetail });
    }
    return route.fulfill({ json: mockProjectDetail });
  });

  await page.route('**/api/projects/demo-project/runtime', (route) =>
    route.fulfill({ json: mockProjectRuntime }),
  );

  // Projects runtime batch（列表页获取所有项目运行时状态）
  await page.route('**/api/projects/runtime**', (route) =>
    route.fulfill({
      json: {
        runtimes: [
          { id: 'demo-project', status: 'running', runningInstances: 1 },
          { id: 'test-project', status: 'stopped', runningInstances: 0 },
        ],
      },
    }),
  );

  await page.route('**/api/projects/demo-project/start', (route) =>
    route.fulfill({ json: { success: true } }),
  );

  await page.route('**/api/projects/demo-project/stop', (route) =>
    route.fulfill({ json: { success: true } }),
  );

  await page.route('**/api/projects/demo-project/validate', (route) =>
    route.fulfill({ json: { valid: true } }),
  );

  // Instances
  await page.route('**/api/instances', (route) => {
    if (route.request().url().includes('/instances/')) return route.fallback();
    return route.fulfill({ json: { instances: mockInstances } });
  });

  await page.route('**/api/instances/inst-001', (route) =>
    route.fulfill({ json: mockInstanceDetail }),
  );

  await page.route('**/api/instances/inst-001/process-tree', (route) =>
    route.fulfill({ json: mockProcessTree }),
  );

  await page.route('**/api/instances/inst-001/resources**', (route) =>
    route.fulfill({ json: mockResources }),
  );

  await page.route('**/api/instances/inst-001/logs**', (route) =>
    route.fulfill({ json: mockInstanceLogs }),
  );

  await page.route('**/api/instances/inst-001/terminate', (route) =>
    route.fulfill({ json: { success: true } }),
  );

  // Drivers
  await page.route('**/api/drivers', (route) =>
    route.fulfill({ json: { drivers: mockDrivers } }),
  );

  await page.route('**/api/drivers/demo-driver', (route) =>
    route.fulfill({ json: mockDriverDetail }),
  );

  // Dashboard instances
  await page.route('**/api/dashboard/instances', (route) =>
    route.fulfill({ json: { instances: mockInstances.slice(0, 3) } }),
  );
}
