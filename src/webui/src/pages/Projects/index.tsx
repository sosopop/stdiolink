import React, { useEffect, useState, useMemo } from 'react';
import { useSearchParams } from 'react-router-dom';
import { Button, Input, Space, Spin, Alert, Select } from 'antd';
import { PlusOutlined } from '@ant-design/icons';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';
import { servicesApi } from '@/api/services';
import { ProjectTable } from './components/ProjectTable';
import { ProjectCreateWizard } from './components/ProjectCreateWizard';
import type { ServiceDetail } from '@/types/service';

const { Search } = Input;

export const ProjectsPage: React.FC = () => {
  const {
    projects, runtimes, loading, error,
    fetchProjects, fetchRuntimes, startProject, stopProject, deleteProject, setEnabled, createProject,
  } = useProjectsStore();
  const { services, fetchServices } = useServicesStore();
  const [search, setSearch] = useState('');
  const [serviceFilter, setServiceFilter] = useState<string | null>(null);
  const [statusFilter, setStatusFilter] = useState<string | null>(null);
  const [createOpen, setCreateOpen] = useState(false);

  useEffect(() => {
    fetchProjects();
    fetchRuntimes();
    fetchServices();
  }, [fetchProjects, fetchRuntimes, fetchServices]);

  const [searchParams, setSearchParams] = useSearchParams();

  useEffect(() => {
    if (searchParams.get('action') === 'create') {
      setCreateOpen(true);
      setSearchParams((prev) => {
        const next = new URLSearchParams(prev);
        next.delete('action');
        return next;
      }, { replace: true });
    }
  }, [searchParams, setSearchParams]);

  const filtered = useMemo(() => {
    let list = projects;
    if (search) {
      const q = search.toLowerCase();
      list = list.filter((p) => p.id.toLowerCase().includes(q) || p.name.toLowerCase().includes(q));
    }
    if (serviceFilter) {
      list = list.filter((p) => p.serviceId === serviceFilter);
    }
    if (statusFilter) {
      list = list.filter((p) => {
        const rt = runtimes[p.id];
        const status = rt?.status ?? 'stopped';
        return status === statusFilter;
      });
    }
    return list;
  }, [projects, runtimes, search, serviceFilter, statusFilter]);

  const getServiceDetail = async (id: string): Promise<ServiceDetail | null> => {
    try {
      return await servicesApi.detail(id);
    } catch {
      return null;
    }
  };

  return (
    <div data-testid="page-projects">
      {error && <Alert type="error" message={error} closable style={{ marginBottom: 16 }} data-testid="projects-error" />}
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 16 }}>
        <Space>
          <Search
            placeholder="Search projects..."
            allowClear
            onChange={(e) => setSearch(e.target.value)}
            style={{ width: 240 }}
            data-testid="search-input"
          />
          <Select
            placeholder="Service"
            allowClear
            onChange={(v) => setServiceFilter(v ?? null)}
            options={services.map((s) => ({ label: s.name, value: s.id }))}
            style={{ width: 160 }}
            data-testid="service-filter"
          />
          <Select
            placeholder="Status"
            allowClear
            onChange={(v) => setStatusFilter(v ?? null)}
            options={[
              { label: 'Running', value: 'running' },
              { label: 'Stopped', value: 'stopped' },
              { label: 'Error', value: 'error' },
            ]}
            style={{ width: 120 }}
            data-testid="status-filter"
          />
        </Space>
        <Button type="primary" icon={<PlusOutlined />} onClick={() => setCreateOpen(true)} data-testid="create-btn">
          New Project
        </Button>
      </div>
      {loading && projects.length === 0 ? (
        <Spin data-testid="loading-spinner" />
      ) : (
        <ProjectTable
          projects={filtered}
          runtimes={runtimes}
          loading={loading}
          onStart={startProject}
          onStop={stopProject}
          onDelete={deleteProject}
          onToggleEnabled={setEnabled}
        />
      )}
      <ProjectCreateWizard
        open={createOpen}
        onClose={() => setCreateOpen(false)}
        onCreate={createProject}
        services={services}
        getServiceDetail={getServiceDetail}
      />
    </div>
  );
};

export default ProjectsPage;
