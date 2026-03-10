import { describe, expect, it } from 'vitest';
import { buildProjectCommandLines } from '../projectCommandLine';

describe('buildProjectCommandLines', () => {
  it('returns empty commands until serviceDir and dataRoot are both ready', () => {
    expect(buildProjectCommandLines({
      projectId: '',
      serviceDir: null,
      dataRoot: 'D:/data',
      config: {},
    })).toEqual({ expanded: '', configFile: '', configFilePath: '' });
  });

  it('builds both expanded and config-file forms', () => {
    expect(buildProjectCommandLines({
      projectId: 'demo-project',
      serviceDir: 'D:/code/stdiolink/release/pkg/data_root/services/demo',
      dataRoot: 'D:/code/stdiolink/release/pkg/data_root',
      config: {
        host: '127.0.0.1',
        nested: { port: 502 },
      },
    })).toEqual({
      expanded: 'stdiolink_service "data_root/services/demo" --data-root="data_root" --config.host="127.0.0.1" --config.nested.port=502',
      configFile: 'stdiolink_service "data_root/services/demo" --data-root="data_root" --config-file="data_root/projects/demo-project/param.json"',
      configFilePath: 'data_root/projects/demo-project/param.json',
    });
  });

  it('uses a non-absolute project param path outside release layouts', () => {
    expect(buildProjectCommandLines({
      projectId: 'demo-project',
      serviceDir: 'D:/data/services/demo',
      dataRoot: 'D:/data',
      config: {
        host: '127.0.0.1',
      },
    })).toEqual({
      expanded: 'stdiolink_service "services/demo" --data-root="." --config.host="127.0.0.1"',
      configFile: 'stdiolink_service "services/demo" --data-root="." --config-file="projects/demo-project/param.json"',
      configFilePath: 'projects/demo-project/param.json',
    });
  });
});
