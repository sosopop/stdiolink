import { create } from 'zustand';
import { DriverLabWsClient } from '@/api/driverlab-ws';
import type { WsMessage } from '@/api/driverlab-ws';
import type { CommandMeta, DriverMeta } from '@/types/driver';

export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

export interface ConnectionState {
  status: ConnectionStatus;
  driverId: string | null;
  runMode: 'oneshot' | 'keepalive';
  pid: number | null;
  connectedAt: number | null;
  meta: DriverMeta | null;
  error: string | null;
}

export interface MessageEntry {
  id: string;
  timestamp: number;
  direction: 'send' | 'recv';
  type: string;
  raw: WsMessage | Record<string, unknown>;
  payload: unknown;
  expanded: boolean;
}

const MAX_MESSAGES = 500;
let msgCounter = 0;

function makeId(): string {
  return `msg-${Date.now()}-${++msgCounter}`;
}

function normalizePayload(msg: WsMessage): unknown {
  if (msg.type === 'stdout' || msg.type === 'error') return msg.message;
  if (msg.type === 'meta') return msg.meta;
  return msg;
}

interface DriverLabState {
  connection: ConnectionState;
  messages: MessageEntry[];
  commands: CommandMeta[];
  selectedCommand: string | null;
  commandParams: Record<string, unknown>;
  executing: boolean;
  autoScroll: boolean;

  connect: (driverId: string, runMode: 'oneshot' | 'keepalive', args?: string[]) => void;
  disconnect: () => void;
  execCommand: (command: string, data: Record<string, unknown>) => void;
  cancelCommand: () => void;
  selectCommand: (name: string) => void;
  setCommandParams: (params: Record<string, unknown>) => void;
  clearMessages: () => void;
  toggleAutoScroll: () => void;
  appendMessage: (entry: MessageEntry) => void;
  handleWsMessage: (msg: WsMessage) => void;

  // internal
  _wsClient: DriverLabWsClient | null;
}

const initialConnection: ConnectionState = {
  status: 'disconnected',
  driverId: null,
  runMode: 'oneshot',
  pid: null,
  connectedAt: null,
  meta: null,
  error: null,
};

export const useDriverLabStore = create<DriverLabState>()((set, get) => ({
  connection: { ...initialConnection },
  messages: [],
  commands: [],
  selectedCommand: null,
  commandParams: {},
  executing: false,
  autoScroll: true,
  _wsClient: null,

  connect: (driverId, runMode, args) => {
    const prev = get()._wsClient;
    if (prev) prev.disconnect();

    const client = new DriverLabWsClient();

    set({
      connection: {
        status: 'connecting',
        driverId,
        runMode,
        pid: null,
        connectedAt: null,
        meta: null,
        error: null,
      },
      messages: [],
      commands: [],
      selectedCommand: null,
      commandParams: {},
      executing: false,
      _wsClient: client,
    });

    client.on('connected', () => {
      set((s) => ({
        connection: { ...s.connection, status: 'connected', connectedAt: Date.now() },
      }));
    });

    client.on('message', (data) => {
      get().handleWsMessage(data as WsMessage);
    });

    client.on('error', () => {
      set((s) => ({
        connection: { ...s.connection, status: 'error', error: 'WebSocket error' },
      }));
    });

    client.on('disconnected', () => {
      set((s) => ({
        connection: { ...s.connection, status: 'disconnected' },
        executing: false,
      }));
    });

    client.connect(driverId, runMode, args);
  },

  disconnect: () => {
    const client = get()._wsClient;
    if (client) client.disconnect();
    set({
      connection: { ...get().connection, status: 'disconnected' },
      executing: false,
      _wsClient: null,
    });
  },

  execCommand: (command, data) => {
    const client = get()._wsClient;
    if (!client) return;
    client.exec(command, data);
    set({ executing: true });

    const entry: MessageEntry = {
      id: makeId(),
      timestamp: Date.now(),
      direction: 'send',
      type: 'exec',
      raw: { type: 'exec', cmd: command, data },
      payload: { cmd: command, data },
      expanded: false,
    };
    get().appendMessage(entry);
  },

  cancelCommand: () => {
    const client = get()._wsClient;
    if (!client) return;
    client.cancel();
    set({ executing: false });

    const entry: MessageEntry = {
      id: makeId(),
      timestamp: Date.now(),
      direction: 'send',
      type: 'cancel',
      raw: { type: 'cancel' },
      payload: null,
      expanded: false,
    };
    get().appendMessage(entry);
  },

  selectCommand: (name) => {
    set({ selectedCommand: name, commandParams: {} });
  },

  setCommandParams: (params) => {
    set({ commandParams: params });
  },

  clearMessages: () => {
    set({ messages: [] });
  },

  toggleAutoScroll: () => {
    set((s) => ({ autoScroll: !s.autoScroll }));
  },

  appendMessage: (entry) => {
    set((s) => {
      const msgs = [...s.messages, entry];
      if (msgs.length > MAX_MESSAGES) {
        return { messages: msgs.slice(msgs.length - MAX_MESSAGES) };
      }
      return { messages: msgs };
    });
  },

  handleWsMessage: (msg) => {
    const entry: MessageEntry = {
      id: makeId(),
      timestamp: Date.now(),
      direction: 'recv',
      type: msg.type,
      raw: msg,
      payload: normalizePayload(msg),
      expanded: false,
    };
    get().appendMessage(entry);

    switch (msg.type) {
      case 'driver.started':
      case 'driver.restarted':
        set((s) => ({
          connection: { ...s.connection, pid: msg.pid as number },
        }));
        break;
      case 'meta': {
        const meta = msg.meta as DriverMeta;
        set((s) => ({
          connection: { ...s.connection, meta },
          commands: meta?.commands ?? [],
        }));
        break;
      }
      case 'stdout': {
        const message = msg.message as Record<string, unknown> | undefined;
        if (message && (message.status === 'done' || message.status === 'error')) {
          set({ executing: false });
        }
        break;
      }
      case 'driver.exited':
        set((s) => {
          if (s.connection.runMode === 'keepalive') {
            return {
              connection: { ...s.connection, status: 'disconnected', pid: null },
              executing: false,
            };
          }
          return { executing: false, connection: { ...s.connection, pid: null } };
        });
        break;
      case 'error':
        set((s) => ({
          connection: { ...s.connection, error: msg.message as string },
        }));
        break;
    }
  },
}));
