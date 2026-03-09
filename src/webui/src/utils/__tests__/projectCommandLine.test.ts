import { describe, expect, it } from 'vitest';
import { buildProjectCommandLines } from '../projectCommandLine';

describe('buildProjectCommandLines', () => {
  it('returns empty commands until serviceDir and dataRoot are both ready', () => {
    expect(buildProjectCommandLines({
      serviceDir: null,
      dataRoot: 'D:/data',
      config: {},
    })).toEqual({ expanded: '', configFile: '' });
  });

  it('builds both expanded and config-file forms', () => {
    expect(buildProjectCommandLines({
      serviceDir: 'D:/data/services/demo',
      dataRoot: 'D:/data',
      config: {
        host: '127.0.0.1',
        nested: { port: 502 },
      },
      configFileName: 'demo-project.config.json',
    })).toEqual({
      expanded: 'stdiolink_service "D:/data/services/demo" --data-root="D:/data" --config.host="127.0.0.1" --config.nested.port=502',
      configFile: 'stdiolink_service "D:/data/services/demo" --data-root="D:/data" --config-file="demo-project.config.json"',
    });
  });
});
