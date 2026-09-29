import { useEffect, useMemo } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import {
  PLATE_STATUS_COMPLETED,
  PLATE_STATUS_PENDING,
  PLATE_STATUS_PRINTING,
} from './types';
import type {
  PrintQueueService,
  PrintQueueSnapshot,
  QueuePlate,
  QueuePlateStatus,
  QueueProject,
} from './types';
import { setPrintQueueService, usePrintQueueStore } from './usePrintQueueStore';

const MODULE = 'print_queue';

interface BridgeResponseBody {
  error_code: number;
  message: string;
  payload: unknown;
}

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isPrintQueueVisible() {
  const hashPath = window.location.hash.replace(/^#/, '').split('?')[0] || '';
  const route = hashPath.startsWith('/') ? hashPath : `/${hashPath}`;
  return route === '/device_page/print_queue' && document.visibilityState !== 'hidden';
}

function asString(value: unknown): string {
  if (typeof value === 'string') return value;
  if (typeof value === 'number') return String(value);
  return '';
}

function asNumber(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function asInteger(value: unknown): number {
  return typeof value === 'number' && Number.isSafeInteger(value) ? value : -1;
}

function toStatus(value: unknown): QueuePlateStatus {
  if (value === PLATE_STATUS_PENDING) return 'pending';
  if (value === PLATE_STATUS_PRINTING) return 'printing';
  if (value === PLATE_STATUS_COMPLETED) return 'completed';
  return 'failed';
}

function toPlate(raw: Record<string, unknown>): QueuePlate {
  return {
    id: asString(raw.id),
    modelId: asString(raw.model_id),
    profileId: asString(raw.profile_id),
    deviceId: asString(raw.device_id),
    name: asString(raw.name),
    profileName: asString(raw.profile_name),
    costTimeSeconds: asNumber(raw.cost_time),
    weightGram: asNumber(raw.weight),
    plateIndex: asInteger(raw.plate_index),
    cover: asString(raw.cover),
    status: toStatus(raw.status),
    available: raw.available !== false,
  };
}

function toProject(raw: Record<string, unknown>): QueueProject {
  const plates = Array.isArray(raw.plates) ? raw.plates : [];
  return {
    id: asString(raw.id),
    name: asString(raw.name),
    cover: asString(raw.cover),
    plates: plates
      .filter((plate): plate is Record<string, unknown> => !!plate && typeof plate === 'object')
      .map(toPlate),
  };
}

export function toSnapshot(payload: unknown, version: number): PrintQueueSnapshot {
  const raw = (payload && typeof payload === 'object') ? payload as Record<string, unknown> : {};
  const projects = Array.isArray(raw.projects) ? raw.projects : [];
  return {
    version,
    projects: projects
      .filter((project): project is Record<string, unknown> => !!project && typeof project === 'object')
      .map(toProject),
  };
}

export function usePrintQueueBridge(): void {
  const request = useDeviceBridge();
  const applySnapshot = usePrintQueueStore((state) => state.applySnapshot);

  const service = useMemo<PrintQueueService>(() => {
    const send = async (
      submod: string,
      action: string,
      payload?: Record<string, unknown>,
    ): Promise<void> => {
      const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
        makeBody(submod, action, payload),
      );
      if (!res.ok) throw new Error(res.error);
      if (res.value.error_code !== 0) {
        throw new Error(res.value.message || 'The print queue request failed.');
      }
    };

    return {
      refresh: () => send('queue', 'list'),
      printPlate: (plate) => send('plate', 'print', {
        plate_id: plate.id,
        model_id: plate.modelId,
        profile_id: plate.profileId,
        plate_index: plate.plateIndex,
        title: plate.name,
        device_id: plate.deviceId,
        cover: plate.cover,
      }),
      deleteProject: (projectId) => send('project', 'delete', { project_id: projectId }),
      deletePlate: (_projectId, plateId) => send('plate', 'delete', { plate_id: plateId }),
      moveProject: (projectId, prevProjectId) =>
        send('project', 'sort', { project_id: projectId, prev_project_id: prevProjectId }),
      movePlate: (plateId, targetProjectId, prevPlateId) =>
        send('plate', 'sort', {
          plate_id: plateId,
          project_id: targetProjectId,
          prev_plate_id: prevPlateId,
        }),
      extractPlate: (plateId, title, cover, prevProjectId) =>
        send('plate', 'extract', {
          plate_id: plateId,
          title,
          cover,
          prev_project_id: prevProjectId,
        }),
    };
  }, [request]);

  useEffect(() => {
    setPrintQueueService(service);
    return () => setPrintQueueService(null);
  }, [service]);

  // The host pushes a snapshot whenever the queue changes; no polling on purpose.
  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      const body = detail?.body;
      if (!body || typeof body !== 'object') return;
      if (body.module !== MODULE || detail.head?.type !== 'report') return;
      if (body.submod !== 'queue' || body.action !== 'list') return;
      if (!isPrintQueueVisible()) return;
      applySnapshot(toSnapshot(body.payload, Date.now()));
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applySnapshot]);

  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      const body = detail?.body;
      if (!body || typeof body !== 'object') return;
      if (body.module !== MODULE || detail.head?.type !== 'report') return;
      if (body.submod !== 'plate' || body.action !== 'print') return;
      usePrintQueueStore.setState({
        error: body.error_code === 0 ? null : (typeof body.message === 'string' ? body.message : 'Unable to prepare the print job.'),
        printingPlateId: null,
      });
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, []);
}
