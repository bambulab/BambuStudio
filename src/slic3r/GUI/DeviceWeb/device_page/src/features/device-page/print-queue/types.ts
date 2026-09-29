// Mirrors hits[].plates[].status from GET /v1/user-service/my/print/projects.
export const PLATE_STATUS_PENDING = 1;
export const PLATE_STATUS_PRINTING = 2;
export const PLATE_STATUS_COMPLETED = 3;

export type QueuePlateStatus = 'pending' | 'printing' | 'completed' | 'failed';

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

export interface PlateDropTarget {
  projectId: string;
  index: number;
}

// Every call resolves once the host has accepted the command, not once the
// cloud round-trip is done; the UI redraws from a pushed snapshot report.
export interface PrintQueueService {
  refresh(): Promise<void>;
  printPlate(plate: QueuePlate): Promise<void>;
  deleteProject(projectId: string): Promise<void>;
  deletePlate(projectId: string, plateId: string): Promise<void>;
  pinProject(projectId: string): Promise<void>;
  pinPlate(projectId: string, plateId: string): Promise<void>;
  movePlate(
    plateId: string,
    sourceProjectId: string,
    targetProjectId: string,
    targetIndex: number,
  ): Promise<void>;
}

export function canPrintQueuePlate(plate: Pick<QueuePlate, 'available' | 'modelId' | 'profileId' | 'status' | 'plateIndex'>): boolean {
  return plate.available && (plate.status === 'pending' || plate.status === 'failed') &&
  plate.modelId.trim().length > 0 && /^[1-9]\d*$/.test(plate.profileId) &&
  Number.isInteger(plate.plateIndex) && plate.plateIndex >= 1 && plate.plateIndex <= 255;
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
