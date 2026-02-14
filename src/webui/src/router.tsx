import { createBrowserRouter, Navigate } from 'react-router-dom';
import { AppLayout } from '@/components/Layout/AppLayout';
import { DashboardPage } from '@/pages/Dashboard';
import { ServicesPage } from '@/pages/Services';
import { ServiceDetailPage } from '@/pages/Services/Detail';
import { ProjectsPage } from '@/pages/Projects';
import { ProjectCreatePage } from '@/pages/Projects/Create';
import { ProjectDetailPage } from '@/pages/Projects/Detail';
import { InstancesPage } from '@/pages/Instances';
import { InstanceDetailPage } from '@/pages/Instances/Detail';
import { DriversPage } from '@/pages/Drivers';
import { DriverDetailPage } from '@/pages/Drivers/Detail';
import { DriverLabPage } from '@/pages/DriverLab';
import { NotFoundPage } from '@/pages/NotFound';

export const router = createBrowserRouter([
  {
    path: '/',
    element: <AppLayout />,
    children: [
      { index: true, element: <Navigate to="/dashboard" replace /> },
      { path: 'dashboard', element: <DashboardPage /> },
      { path: 'services', element: <ServicesPage /> },
      { path: 'services/:id', element: <ServiceDetailPage /> },
      { path: 'projects', element: <ProjectsPage /> },
      { path: 'projects/create', element: <ProjectCreatePage /> },
      { path: 'projects/:id', element: <ProjectDetailPage /> },
      { path: 'instances', element: <InstancesPage /> },
      { path: 'instances/:id', element: <InstanceDetailPage /> },
      { path: 'drivers', element: <DriversPage /> },
      { path: 'drivers/:id', element: <DriverDetailPage /> },
      { path: 'driverlab', element: <DriverLabPage /> },
      { path: '*', element: <NotFoundPage /> },
    ],
  },
]);
