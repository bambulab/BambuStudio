import { create } from 'zustand';
import type {
  DeleteTarget,
  PrintQueueService,
  PrintQueueSnapshot,
  QueueDragSource,
  QueueDropTarget,
  QueuePlate,
  QueueProject,
} from './types';
import { PREV_SIBLING_TOP, resolveMoveCommand } from './queueOrder';

const EMPTY_SNAPSHOT: PrintQueueSnapshot = { version: 0, projects: [] };

let activeService: PrintQueueService | null = null;

export function setPrintQueueService(service: PrintQueueService | null): void {
  activeService = service;
}

function requireService(): PrintQueueService {
  if (!activeService) throw new Error('The print queue service is unavailable.');
  return activeService;
}

function cloneSnapshot(snapshot: PrintQueueSnapshot): PrintQueueSnapshot {
  return structuredClone(snapshot);
}

// Commands are acknowledged immediately and the real data arrives in a later
// report, so every in-flight command arms a watchdog: without it a host that
// dies mid-request would leave the spinner up forever.
const SETTLE_TIMEOUT_MS = 15_000;
let settleTimer: number | null = null;

function clearSettleTimeout(): void {
  if (settleTimer === null) return;
  window.clearTimeout(settleTimer);
  settleTimer = null;
}

interface PrintQueueState {
  snapshot: PrintQueueSnapshot;
  initialized: boolean;
  loading: boolean;
  refreshing: boolean;
  mutationPending: boolean;
  printingPlateId: string | null;
  error: string | null;
  expandedProjects: Record<string, boolean>;
  deleteTarget: DeleteTarget | null;
  dragSource: QueueDragSource | null;
  dropTarget: QueueDropTarget | null;
  applySnapshot: (snapshot: PrintQueueSnapshot) => void;
  initialize: () => Promise<void>;
  refresh: () => Promise<void>;
  toggleProject: (projectId: string) => void;
  expandProject: (projectId: string) => void;
  requestDelete: (target: DeleteTarget) => void;
  cancelDelete: () => void;
  confirmDelete: () => Promise<void>;
  pinProject: (projectId: string) => Promise<void>;
  pinPlate: (projectId: string, plateId: string) => Promise<void>;
  beginDrag: (source: QueueDragSource) => void;
  setDropTarget: (target: QueueDropTarget | null) => void;
  cancelDrag: () => void;
  commitDrag: () => Promise<void>;
  clearError: () => void;
  printPlate: (plate: QueuePlate) => Promise<void>;
}

function mergeExpandedState(
  snapshot: PrintQueueSnapshot,
  current: Record<string, boolean>,
): Record<string, boolean> {
  return Object.fromEntries(
    snapshot.projects.map((project) => [project.id, current[project.id] ?? true]),
  );
}

function toMessage(error: unknown, fallback: string): string {
  return error instanceof Error && error.message ? error.message : fallback;
}

export const usePrintQueueStore = create<PrintQueueState>((set, get) => ({
  snapshot: cloneSnapshot(EMPTY_SNAPSHOT),
  initialized: false,
  loading: false,
  refreshing: false,
  mutationPending: false,
  printingPlateId: null,
  error: null,
  expandedProjects: {},
  deleteTarget: null,
  dragSource: null,
  dropTarget: null,

  applySnapshot: (snapshot) => {
    clearSettleTimeout();
    set({
      snapshot,
      initialized: true,
      loading: false,
      refreshing: false,
      mutationPending: false,
      expandedProjects: mergeExpandedState(snapshot, get().expandedProjects),
      // A pushed snapshot reorders the rows under the pointer, so the slot the
      // user was aiming at stops meaning anything: drop the drag instead of
      // committing it against a list that has already changed.
      dragSource: null,
      dropTarget: null,
    });
  },

  initialize: async () => {
    if (get().initialized || get().loading) return;
    set({ loading: true, error: null });
    try {
      await requireService().refresh();
      armSettleTimeout(set);
    } catch (error) {
      set({ loading: false, error: toMessage(error, 'Unable to load the queue.') });
    }
  },

  refresh: async () => {
    set({ refreshing: true, error: null });
    try {
      await requireService().refresh();
      armSettleTimeout(set);
    } catch (error) {
      set({ refreshing: false, error: toMessage(error, 'Unable to refresh the queue.') });
    }
  },

  toggleProject: (projectId) => {
    set((state) => ({
      expandedProjects: {
        ...state.expandedProjects,
        [projectId]: !(state.expandedProjects[projectId] ?? true),
      },
    }));
  },

  expandProject: (projectId) => {
    if (get().expandedProjects[projectId] ?? true) return;
    set((state) => ({
      expandedProjects: { ...state.expandedProjects, [projectId]: true },
    }));
  },

  requestDelete: (target) => set({ deleteTarget: target }),
  cancelDelete: () => set({ deleteTarget: null }),

  confirmDelete: async () => {
    const target = get().deleteTarget;
    if (!target) return;
    await runMutation(set, get, async (service) => {
      if (target.kind === 'project') await service.deleteProject(target.projectId);
      else await service.deletePlate(target.projectId, target.plateId);
      set({ deleteTarget: null });
    });
  },

  pinProject: async (projectId) => {
    await runMutation(set, get, (service) => service.moveProject(projectId, PREV_SIBLING_TOP));
  },

  pinPlate: async (projectId, plateId) => {
    await runMutation(set, get, (service) => service.movePlate(plateId, projectId, PREV_SIBLING_TOP));
  },

  beginDrag: (source) => {
    if (get().mutationPending) return;
    set({ dragSource: source, dropTarget: null });
  },

  setDropTarget: (dropTarget) => set({ dropTarget }),

  cancelDrag: () => {
    set({ dragSource: null, dropTarget: null });
  },

  commitDrag: async () => {
    const { snapshot, dragSource, dropTarget } = get();
    const command = dragSource && dropTarget
      ? resolveMoveCommand(snapshot, dragSource, dropTarget)
      : null;
    // Clear the drag highlight before awaiting: the request only resolves once
    // the host accepted it, and the row must not stay lit until then.
    get().cancelDrag();
    if (!command) return;

    await runMutation(set, get, (service) => {
      switch (command.kind) {
        case 'moveProject':
          return service.moveProject(command.projectId, command.prevProjectId);
        case 'movePlate':
          return service.movePlate(command.plateId, command.targetProjectId, command.prevPlateId);
        case 'extractPlate':
          return service.extractPlate(
            command.plateId,
            command.title,
            command.cover,
            command.prevProjectId,
          );
      }
    });
  },

  clearError: () => set({ error: null }),

  printPlate: async (plate) => {
    if (get().printingPlateId || get().mutationPending) return;
    set({ printingPlateId: plate.id, error: null });
    try {
      await requireService().printPlate(plate);
    } catch (error) {
      set({ error: toMessage(error, 'Unable to prepare the print job.') });
      set({ printingPlateId: null });
    }
  },
}));

function armSettleTimeout(set: (partial: Partial<PrintQueueState>) => void): void {
  clearSettleTimeout();
  settleTimer = window.setTimeout(() => {
    settleTimer = null;
    set({
      loading: false,
      refreshing: false,
      mutationPending: false,
      error: 'The print queue did not respond.',
    });
  }, SETTLE_TIMEOUT_MS);
}

async function runMutation(
  set: (partial: Partial<PrintQueueState>) => void,
  get: () => PrintQueueState,
  operation: (service: PrintQueueService) => Promise<void>,
): Promise<void> {
  if (get().mutationPending) return;
  set({ mutationPending: true, error: null });
  try {
    await operation(requireService());
    armSettleTimeout(set);
  } catch (error) {
    set({ mutationPending: false, error: toMessage(error, 'Operation failed.') });
  }
}

export function canDeletePlate(plate: QueuePlate): boolean {
  return plate.status !== 'printing';
}

export function canDeleteProject(project: QueueProject): boolean {
  return project.plates.every((plate) => plate.status !== 'printing');
}

export function canPinPlate(plate: QueuePlate): boolean {
  return plate.status === 'pending';
}

export function canPinProject(project: QueueProject): boolean {
  return project.plates.length > 0 && project.plates.every((plate) => plate.status === 'pending');
}

export function canDragPlate(plate: QueuePlate): boolean {
  return plate.status === 'pending';
}

// Reordering a project moves every plate it holds, so it follows the same rule
// as Pin to top: nothing in it may have started yet.
export function canDragProject(project: QueueProject): boolean {
  return canPinProject(project);
}
