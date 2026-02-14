import React, { useEffect, useState, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Input, Select, Space } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { useInstancesStore } from '@/stores/useInstancesStore';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { InstancesTable } from '@/components/Instances/InstancesTable';

const { Search } = Input;

export const InstancesPage: React.FC = () => {
  const { t } = useTranslation();
  const { instances, loading, fetchInstances, terminateInstance } = useInstancesStore();
  const { projects, fetchProjects } = useProjectsStore();
  const [search, setSearch] = useState('');
  const [projectFilter, setProjectFilter] = useState<string | undefined>();
  const [statusFilter, setStatusFilter] = useState<string | undefined>();

  useEffect(() => {
    fetchInstances();
    fetchProjects();
  }, [fetchInstances, fetchProjects]);

  const filtered = useMemo(() => {
    return instances.filter((inst) => {
      if (search) {
        const q = search.toLowerCase();
        if (!inst.id.toLowerCase().includes(q) && !inst.projectId.toLowerCase().includes(q)) return false;
      }
      if (projectFilter && inst.projectId !== projectFilter) return false;
      if (statusFilter && inst.status !== statusFilter) return false;
      return true;
    });
  }, [instances, search, projectFilter, statusFilter]);

  return (
    <div data-testid="page-instances">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <h2 style={{ margin: 0 }}>{t('instances.title')}</h2>
        <Button icon={<ReloadOutlined />} onClick={() => fetchInstances()} loading={loading} data-testid="refresh-btn">
          {t('instances.refresh')}
        </Button>
      </div>
      <Space style={{ marginBottom: 16 }} wrap>
        <Search
          placeholder={t('instances.search_placeholder')}
          allowClear
          onChange={(e) => setSearch(e.target.value)}
          style={{ width: 240 }}
          data-testid="instance-search"
        />
        <Select
          placeholder={t('instances.filter_project')}
          allowClear
          onChange={(v) => setProjectFilter(v)}
          style={{ width: 180 }}
          options={projects.map((p) => ({ label: p.name || p.id, value: p.id }))}
          data-testid="project-filter"
        />
        <Select
          placeholder={t('instances.filter_status')}
          allowClear
          onChange={(v) => setStatusFilter(v)}
          style={{ width: 140 }}
          options={['running', 'stopped', 'failed', 'crashed'].map((s) => ({ label: s, value: s }))}
          data-testid="status-filter"
        />
      </Space>
      <InstancesTable instances={filtered} onTerminate={terminateInstance} />
    </div>
  );
};

export default InstancesPage;
