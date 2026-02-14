import { describe, it, expect } from 'vitest';
import { render, screen, waitFor } from '@testing-library/react';
import { RouterProvider, createMemoryRouter } from 'react-router-dom';
import { ConfigProvider } from 'antd';
import { AppLayout } from '@/components/Layout/AppLayout';
import { DashboardPage } from '@/pages/Dashboard';
import { ServicesPage } from '@/pages/Services';
import { ProjectsPage } from '@/pages/Projects';
import { NotFoundPage } from '@/pages/NotFound';
import { Navigate } from 'react-router-dom';

function createTestRouter(initialPath: string) {
  return createMemoryRouter(
    [
      {
        path: '/',
        element: <AppLayout />,
        children: [
          { index: true, element: <Navigate to="/dashboard" replace /> },
          { path: 'dashboard', element: <DashboardPage /> },
          { path: 'services', element: <ServicesPage /> },
          { path: 'projects', element: <ProjectsPage /> },
          { path: '*', element: <NotFoundPage /> },
        ],
      },
    ],
    { initialEntries: [initialPath] },
  );
}

function renderRoute(path: string) {
  const router = createTestRouter(path);
  return render(
    <ConfigProvider>
      <RouterProvider router={router} />
    </ConfigProvider>,
  );
}

describe('Router', () => {
  it('redirects / to /dashboard', async () => {
    renderRoute('/');
    await waitFor(() => {
      expect(screen.getByTestId('page-dashboard')).toBeDefined();
    });
  });

  it('renders Dashboard page at /dashboard', async () => {
    renderRoute('/dashboard');
    await waitFor(() => {
      expect(screen.getByTestId('page-dashboard')).toBeDefined();
    });
  });

  it('renders Services page at /services', async () => {
    renderRoute('/services');
    await waitFor(() => {
      expect(screen.getByTestId('page-services')).toBeDefined();
    });
  });

  it('renders Projects page at /projects', async () => {
    renderRoute('/projects');
    await waitFor(() => {
      expect(screen.getByTestId('page-projects')).toBeDefined();
    });
  });

  it('renders 404 page for unknown routes', async () => {
    renderRoute('/unknown-route');
    await waitFor(() => {
      expect(screen.getByText('404')).toBeDefined();
    });
  });
});
