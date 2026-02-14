import { create } from 'zustand';
import { projectsApi } from '@/api/projects';
import type { Project, ProjectRuntime, CreateProjectRequest, UpdateProjectRequest } from '@/types/project';

interface ProjectsState {
  projects: Project[];
  runtimes: Record<string, ProjectRuntime>;
  currentProject: Project | null;
  currentRuntime: ProjectRuntime | null;
  loading: boolean;
  error: string | null;

  fetchProjects: () => Promise<void>;
  fetchProjectDetail: (id: string) => Promise<void>;
  fetchRuntimes: () => Promise<void>;
  fetchRuntime: (id: string) => Promise<void>;
  createProject: (data: CreateProjectRequest) => Promise<boolean>;
  updateProject: (id: string, data: UpdateProjectRequest) => Promise<boolean>;
  deleteProject: (id: string) => Promise<boolean>;
  startProject: (id: string) => Promise<boolean>;
  stopProject: (id: string) => Promise<boolean>;
  reloadProject: (id: string) => Promise<boolean>;
  setEnabled: (id: string, enabled: boolean) => Promise<boolean>;
}

export const useProjectsStore = create<ProjectsState>()((set, get) => ({
  projects: [],
  runtimes: {},
  currentProject: null,
  currentRuntime: null,
  loading: false,
  error: null,

  fetchProjects: async () => {
    try {
      set({ loading: true, error: null });
      const data = await projectsApi.list();
      set({ projects: data.projects, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch projects', loading: false });
    }
  },

  fetchProjectDetail: async (id: string) => {
    try {
      set({ loading: true, error: null });
      const project = await projectsApi.detail(id);
      set({ currentProject: project, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch project detail', loading: false });
    }
  },

  fetchRuntimes: async () => {
    try {
      const data = await projectsApi.runtimeBatch();
      const map: Record<string, ProjectRuntime> = {};
      for (const rt of data.runtimes) {
        map[rt.id] = rt;
      }
      set({ runtimes: map });
    } catch {
      // silently fail
    }
  },

  fetchRuntime: async (id: string) => {
    try {
      const rt = await projectsApi.runtime(id);
      set((s) => ({ currentRuntime: rt, runtimes: { ...s.runtimes, [id]: rt } }));
    } catch {
      // silently fail
    }
  },

  createProject: async (data) => {
    try {
      set({ error: null });
      await projectsApi.create(data);
      await get().fetchProjects();
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to create project' });
      return false;
    }
  },

  updateProject: async (id, data) => {
    try {
      set({ error: null });
      const updated = await projectsApi.update(id, data);
      set({ currentProject: updated });
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to update project' });
      return false;
    }
  },

  deleteProject: async (id) => {
    try {
      set({ error: null });
      await projectsApi.delete(id);
      set((s) => ({ projects: s.projects.filter((p) => p.id !== id) }));
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to delete project' });
      return false;
    }
  },

  startProject: async (id) => {
    try {
      set({ error: null });
      await projectsApi.start(id);
      await get().fetchRuntime(id);
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to start project' });
      return false;
    }
  },

  stopProject: async (id) => {
    try {
      set({ error: null });
      await projectsApi.stop(id);
      await get().fetchRuntime(id);
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to stop project' });
      return false;
    }
  },

  reloadProject: async (id) => {
    try {
      set({ error: null });
      const project = await projectsApi.reload(id);
      set({ currentProject: project });
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to reload project' });
      return false;
    }
  },

  setEnabled: async (id, enabled) => {
    try {
      set({ error: null });
      const updated = await projectsApi.setEnabled(id, enabled);
      set((s) => ({
        projects: s.projects.map((p) => (p.id === id ? updated : p)),
        currentProject: s.currentProject?.id === id ? updated : s.currentProject,
      }));
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to update enabled state' });
      return false;
    }
  },
}));
