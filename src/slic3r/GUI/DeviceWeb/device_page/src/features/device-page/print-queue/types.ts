// Mirrors hits[].plates[].status from GET /v1/user-service/my/print/projects.
export const PLATE_STATUS_PENDING = 1;
export const PLATE_STATUS_PRINTING = 2;
export const PLATE_STATUS_COMPLETED = 3;

export type QueuePlateStatus = 'pending' | 'printing' | 'completed' | 'failed';

export type QueueProjectStatus = 'in-progress' | 'completed';

export interface QueuePlate {
  // hits[].plates[].id, an int64 kept as a string so no precision is lost.
  id: string;
  // Model/profile IDs are also strings because queue-service IDs are int64.
  modelId: string;
  profileId: string;
  deviceId: string;
  name: string;
  // No field for this in the queue payload; needs GET /my/model/profile.
  profileName: string;
  costTimeSeconds: number;
  weightGram: number;
  // hits[].plates[].plateIndex: one-based plate number inside its own model, shown
  // as the row-label suffix. Not the position within the queue project.
  plateIndex: number;
  cover: string;
  status: QueuePlateStatus;
  available: boolean;
}

export interface QueueProject {
  id: string;
  name: string;
  cover: string;
  plates: QueuePlate[];
}

export interface PrintQueueSnapshot {
  version: number;
  projects: QueueProject[];
}

export type DeleteTarget =
  | { kind: 'project'; projectId: string; label: string }
  | { kind: 'plate'; projectId: string; plateId: string; label: string };

// What the pointer picked up. Dragging a whole project reorders the queue,
// dragging a plate out of a multi-plate project moves that plate; a
// single-plate project is both, so it carries the plate id as well.
export type QueueDragSource =
  | { kind: 'project'; projectId: string; single: boolean; plateId: string | null }
  | { kind: 'plate'; projectId: string; plateId: string };

// Where it would land. `index` is a slot in the rendered list, which still
// counts the dragged row; queueOrder.ts compensates for that when it builds
// the wire params.
export type QueueDropTarget =
  | { kind: 'project'; index: number }
  | { kind: 'plate'; projectId: string; index: number };

// Every call resolves once the host has accepted the command, not once the
// cloud round-trip is done; the UI redraws from a pushed snapshot report.
export interface PrintQueueService {
  refresh(): Promise<void>;
  printPlate(plate: QueuePlate): Promise<void>;
  deleteProject(projectId: string): Promise<void>;
  deletePlate(projectId: string, plateId: string): Promise<void>;
  // Ordering is expressed as "put me after this sibling"; see PREV_SIBLING_TOP.
  moveProject(projectId: string, prevProjectId: string): Promise<void>;
  movePlate(plateId: string, targetProjectId: string, prevPlateId: string): Promise<void>;
  extractPlate(
    plateId: string,
    title: string,
    cover: string,
    prevProjectId: string,
  ): Promise<void>;
}

export function canPrintQueuePlate(plate: Pick<QueuePlate, 'available' | 'modelId' | 'profileId' | 'status' | 'plateIndex'>): boolean {
  return plate.available && (plate.status === 'pending' || plate.status === 'failed') &&
  plate.modelId.trim().length > 0 && /^[1-9]\d*$/.test(plate.profileId) &&
  Number.isInteger(plate.plateIndex) && plate.plateIndex >= 1 && plate.plateIndex <= 255;
}

// A project is Completed only once every plate it holds has settled - succeeded or
// cancelled. One plate still on the bed or still waiting its turn keeps the whole
// project In progress. An empty project has nothing left to run, so it counts as
// Completed.
export function projectStatus(project: QueueProject): QueueProjectStatus {
  const running = project.plates.some(
    (plate) => plate.status === 'printing' || plate.status === 'pending',
  );
  return running ? 'in-progress' : 'completed';
}

// A project holding a single plate is presented as one row: the plate is the
// project (Figma node 5051:25204 — no chevron, project-level index badge, and
// the plate's own duration / weight instead of a plate-count summary).
export function isSinglePlateProject(project: QueueProject): boolean {
  return project.plates.length === 1;
}

export function projectCover(project: QueueProject): string {
  return project.cover || project.plates[0]?.cover || '';
}

// Mirrors short_time(get_time_dhms(seconds)) from libslic3r/Utils.hpp.
export function formatCostTime(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return '0s';

  let days = Math.floor(seconds / 86400);
  let rest = seconds - days * 86400;
  let hours = Math.floor(rest / 3600);
  rest -= hours * 3600;
  let minutes = Math.floor(rest / 60);
  rest -= minutes * 60;
  const secs = Math.floor(rest);

  if (days + hours > 0 && secs >= 30) {
    minutes += 1;
    if (minutes === 60) {
      minutes = 0;
      hours += 1;
      if (hours === 24) {
        hours = 0;
        days += 1;
      }
    }
  }

  if (days > 0) return `${days}d${hours}h${minutes}m`;
  if (hours > 0) return `${hours}h${minutes}m`;
  if (minutes > 0) return `${minutes}m${secs}s`;
  if (secs >= 1) return `${secs}s`;
  if (seconds < 1) return '<1s';
  return '0s';
}

// Matches sprintf("%.2f g", weight) on the Send-print dialog.
export function formatWeight(gram: number): string {
  if (!Number.isFinite(gram) || gram < 0) return '0.00 g';
  return `${gram.toFixed(2)} g`;
}
