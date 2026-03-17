import '@testing-library/jest-dom';
import i18n from './i18n';

// Use deterministic English translations in tests.
void i18n.changeLanguage('en');

// React Router creates Request objects during client-side redirects. In the
// Vitest jsdom environment, Node's Request implementation can reject jsdom's
// AbortSignal instance, so replace it with a small compatibility shim only
// when that mismatch is detected.
function installRequestCompatIfNeeded() {
  if (typeof globalThis.Request === 'undefined' || typeof globalThis.AbortController === 'undefined') {
    return;
  }

  try {
    void new globalThis.Request('http://localhost/', {
      signal: new globalThis.AbortController().signal,
    });
    return;
  } catch {
    class RequestCompat {
      url: string;
      method: string;
      headers: Headers;
      signal?: AbortSignal;
      credentials: RequestCredentials;
      mode: RequestMode;
      redirect: RequestRedirect;
      referrer: string;

      constructor(input: string | URL | RequestCompat, init: RequestInit = {}) {
        const sourceUrl = input instanceof RequestCompat ? input.url : String(input);
        this.url = sourceUrl;
        this.method = init.method ?? 'GET';
        this.headers = new Headers(init.headers);
        this.signal = init.signal;
        this.credentials = init.credentials ?? 'same-origin';
        this.mode = init.mode ?? 'cors';
        this.redirect = init.redirect ?? 'follow';
        this.referrer = init.referrer ?? 'about:client';
      }

      clone() {
        return new RequestCompat(this.url, {
          method: this.method,
          headers: this.headers,
          signal: this.signal,
          credentials: this.credentials,
          mode: this.mode,
          redirect: this.redirect,
          referrer: this.referrer,
        });
      }
    }

    (globalThis as any).Request = RequestCompat;
  }
}

installRequestCompatIfNeeded();

// Mock window.matchMedia for Ant Design responsive components
Object.defineProperty(window, 'matchMedia', {
  writable: true,
  value: (query: string) => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: () => {},
    removeListener: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    dispatchEvent: () => false,
  }),
});

// Mock ResizeObserver for Ant Design components
class MockResizeObserver {
  observe() {}
  unobserve() {}
  disconnect() {}
}
if (typeof globalThis.ResizeObserver === 'undefined') {
  (globalThis as any).ResizeObserver = MockResizeObserver;
}

// Mock EventSource for SSE tests
class MockEventSource {
  url: string;
  onopen: ((ev: Event) => void) | null = null;
  onerror: ((ev: Event) => void) | null = null;
  constructor(url: string) {
    this.url = url;
  }
  addEventListener() {}
  removeEventListener() {}
  close() {}
}

if (typeof globalThis.EventSource === 'undefined') {
  (globalThis as any).EventSource = MockEventSource;
}
