import { useCallback, useEffect, useRef } from 'react';
import type {
  PointerEvent as ReactPointerEvent,
  RefObject,
} from 'react';
import { useTranslation } from 'react-i18next';
import {
  ChevronIcon,
  ClockIcon,
  CloseIcon,
  DeleteIcon,
  MaterialIcon,
  PinIcon,
  PrintIcon,
} from './icons';
import type { DeleteTarget, PlateDropTarget, QueuePlate, QueueProject } from './types';
import { canPrintQueuePlate, formatCostTime, formatWeight, isSinglePlateProject, projectCover } from './types';
import {
  canDeletePlate,
  canDeleteProject,
  canDragPlate,
  canPinPlate,
  canPinProject,
  usePrintQueueStore,
} from './usePrintQueueStore';
import { usePrintQueueBridge } from './usePrintQueueBridge';
import './print-queue.css';

interface ThumbnailProps {
  cover: string;
  status?: QueuePlate['status'];
}

function QueueThumbnail({ cover, status }: ThumbnailProps) {
  const { t } = useTranslation();
  // Spelled out rather than looked up from a key table: i18next-parser only
  // extracts literal t() arguments, so a table would keep these labels out of
  // the catalog entirely.
  const statusLabel = status === 'printing'
    ? t('Printing')
    : status === 'completed'
      ? t('Completed')
      : status === 'failed'
        ? t('Print failed')
        : '';

  return (
    <div className="queue-thumbnail">
      {cover
        ? <img className="queue-thumbnail__art" src={cover} alt="" draggable={false} />
        : <div className="queue-thumbnail__art queue-thumbnail__art--empty" aria-hidden="true" />}
      {statusLabel && (
        <span className={`queue-thumbnail__status queue-thumbnail__status--${status}`}>
          {statusLabel}
        </span>
      )}
    </div>
  );
}

function QueueIndexBadge({ children }: { children: string }) {
  return <span className="queue-index-badge">{children}</span>;
}

function PlateMeta({ plate }: { plate: QueuePlate }) {
  return (
    <div className="queue-row-copy__meta">
      <span><ClockIcon />{formatCostTime(plate.costTimeSeconds)}</span>
      <span><MaterialIcon />{formatWeight(plate.weightGram)}</span>
    </div>
  );
}

interface QueueActionsProps {
  testIdPrefix: string;
  deleteEnabled: boolean;
  pinEnabled: boolean;
  pending: boolean;
  onDelete: () => void;
  onPin: () => void;
  printEnabled?: boolean;
  printPending?: boolean;
  onPrint?: () => void;
}

function QueueActions({
  testIdPrefix,
  deleteEnabled,
  pinEnabled,
  pending,
  onDelete,
  onPin,
  printEnabled = false,
  printPending = false,
  onPrint,
}: QueueActionsProps) {
  const { t } = useTranslation();
  return (
    <div className="queue-actions" data-testid={`${testIdPrefix}-actions`}>
      <button
        type="button"
        className="queue-action queue-action--delete"
        data-testid={`${testIdPrefix}-delete`}
        aria-label={t('Delete')}
        disabled={!deleteEnabled || pending}
        onClick={(event) => {
          event.stopPropagation();
          onDelete();
        }}
      >
        <DeleteIcon />
      </button>
      <button
        type="button"
        className="queue-action queue-action--pin"
        data-testid={`${testIdPrefix}-pin`}
        aria-label={t('Pin to top')}
        disabled={!pinEnabled || pending}
        onClick={(event) => {
          event.stopPropagation();
          onPin();
        }}
      >
        <PinIcon />
      </button>
      <button
        type="button"
        className="queue-action queue-action--print"
        data-testid={`${testIdPrefix}-print`}
        aria-label={printPending ? t('Preparing...') : t('Print')}
        disabled={!onPrint || !printEnabled || pending || printPending}
        onClick={(event) => {
          event.stopPropagation();
          onPrint?.();
        }}
      >
        <PrintIcon />
      </button>
    </div>
  );
}

const DRAG_START_THRESHOLD_PX = 4;
const DRAG_AUTO_SCROLL_EDGE_PX = 48;
const DRAG_AUTO_SCROLL_MAX_STEP_PX = 18;
const DRAG_EXPAND_DELAY_MS = 450;

type BeginPointerDrag = (
  event: ReactPointerEvent<HTMLElement>,
  projectId: string,
  plateId: string,
) => void;

interface ActiveDrag {
  pointerId: number;
  projectId: string;
  plateId: string;
  originY: number;
  pointerY: number;
  started: boolean;
  hoverProjectId: string | null;
  hoverSince: number;
}

// Hit-test the rendered rows instead of relying on per-row dragover events: the
// pointer may sit still while the list auto-scrolls underneath it, and only a
// geometry query stays correct in that case.
function resolveDropTarget(container: HTMLElement, clientY: number): PlateDropTarget | null {
  const rows = Array.from(container.querySelectorAll<HTMLElement>('[data-queue-row]'));
  let last: PlateDropTarget | null = null;

  for (const row of rows) {
    const rect = row.getBoundingClientRect();
    const projectId = row.dataset.projectId;
    if (rect.height === 0 || !projectId) continue;

    if (row.dataset.queueRow === 'project') {
      // Dropping onto a header appends to that project: when it is collapsed
      // there is no visible position to aim at.
      const plateCount = Number(row.dataset.plateCount ?? '0');
      if (clientY <= rect.bottom) return { projectId, index: plateCount };
      last = { projectId, index: plateCount };
      continue;
    }

    const plateIndex = Number(row.dataset.plateIndex ?? '0');
    if (clientY <= rect.bottom) {
      const insertAfter = clientY >= rect.top + rect.height / 2;
      return { projectId, index: plateIndex + (insertAfter ? 1 : 0) };
    }
    last = { projectId, index: plateIndex + 1 };
  }

  return last;
}

function autoScrollStep(distanceIntoEdge: number): number {
  return Math.min(DRAG_AUTO_SCROLL_MAX_STEP_PX, 2 + distanceIntoEdge / 4);
}

function usePlateDrag(scrollRef: RefObject<HTMLDivElement | null>): BeginPointerDrag {
  const beginDrag = usePrintQueueStore((state) => state.beginDrag);
  const setDropTarget = usePrintQueueStore((state) => state.setDropTarget);
  const cancelDrag = usePrintQueueStore((state) => state.cancelDrag);
  const commitDrag = usePrintQueueStore((state) => state.commitDrag);
  const expandProject = usePrintQueueStore((state) => state.expandProject);

  const dragRef = useRef<ActiveDrag | null>(null);
  const rafRef = useRef(0);
  const teardownRef = useRef<(() => void) | null>(null);

  useEffect(() => () => teardownRef.current?.(), []);

  const applyDropTarget = useCallback((clientY: number) => {
    const container = scrollRef.current;
    const drag = dragRef.current;
    if (!container || !drag) return;

    const target = resolveDropTarget(container, clientY);
    setDropTarget(target);

    // Hovering a collapsed project long enough opens it, so the plate can be
    // aimed at a precise slot instead of only being appended.
    if (!target || target.projectId !== drag.hoverProjectId) {
      drag.hoverProjectId = target?.projectId ?? null;
      drag.hoverSince = performance.now();
      return;
    }
    if (performance.now() - drag.hoverSince >= DRAG_EXPAND_DELAY_MS) {
      expandProject(target.projectId);
    }
  }, [expandProject, scrollRef, setDropTarget]);

  return useCallback<BeginPointerDrag>((event, projectId, plateId) => {
    if (event.button !== 0 || dragRef.current) return;
    const container = scrollRef.current;
    if (!container) return;

    dragRef.current = {
      pointerId: event.pointerId,
      projectId,
      plateId,
      originY: event.clientY,
      pointerY: event.clientY,
      started: false,
      hoverProjectId: null,
      hoverSince: 0,
    };

    const runAutoScroll = () => {
      rafRef.current = window.requestAnimationFrame(runAutoScroll);
      const drag = dragRef.current;
      if (!drag?.started) return;

      const rect = container.getBoundingClientRect();
      const aboveTop = rect.top + DRAG_AUTO_SCROLL_EDGE_PX - drag.pointerY;
      const belowBottom = drag.pointerY - (rect.bottom - DRAG_AUTO_SCROLL_EDGE_PX);
      const delta = aboveTop > 0
        ? -autoScrollStep(aboveTop)
        : belowBottom > 0
          ? autoScrollStep(belowBottom)
          : 0;
      if (delta === 0) return;

      const before = container.scrollTop;
      container.scrollTop = before + delta;
      if (container.scrollTop !== before) applyDropTarget(drag.pointerY);
    };

    const handleMove = (moveEvent: PointerEvent) => {
      const drag = dragRef.current;
      if (!drag || moveEvent.pointerId !== drag.pointerId) return;
      drag.pointerY = moveEvent.clientY;

      if (!drag.started) {
        if (Math.abs(moveEvent.clientY - drag.originY) < DRAG_START_THRESHOLD_PX) return;
        drag.started = true;
        beginDrag(drag.projectId, drag.plateId);
      }
      moveEvent.preventDefault();
      applyDropTarget(moveEvent.clientY);
    };

    const finish = (commit: boolean) => {
      teardownRef.current = null;
      window.removeEventListener('pointermove', handleMove);
      window.removeEventListener('pointerup', handleUp);
      window.removeEventListener('pointercancel', handleCancel);
      if (rafRef.current) {
        window.cancelAnimationFrame(rafRef.current);
        rafRef.current = 0;
      }

      const drag = dragRef.current;
      dragRef.current = null;
      if (!drag?.started) {
        cancelDrag();
        return;
      }
      if (commit) void commitDrag();
      else cancelDrag();
    };

    const handleUp = (upEvent: PointerEvent) => {
      if (upEvent.pointerId !== dragRef.current?.pointerId) return;
      finish(true);
    };

    const handleCancel = () => finish(false);

    window.addEventListener('pointermove', handleMove);
    window.addEventListener('pointerup', handleUp);
    window.addEventListener('pointercancel', handleCancel);
    teardownRef.current = () => finish(false);

    // Keeps the stream alive when the pointer leaves the WebView bounds.
    event.currentTarget.setPointerCapture?.(event.pointerId);
    rafRef.current = window.requestAnimationFrame(runAutoScroll);
  }, [applyDropTarget, beginDrag, cancelDrag, commitDrag, scrollRef]);
}

interface PlateRowProps {
  projectId: string;
  projectNumber: number;
  plate: QueuePlate;
  plateIndex: number;
  beginPointerDrag: BeginPointerDrag;
}

function PlateRow({
  projectId,
  projectNumber,
  plate,
  plateIndex,
  beginPointerDrag,
}: PlateRowProps) {
  const { t } = useTranslation();
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const draggingPlateId = usePrintQueueStore((state) => state.draggingPlateId);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinPlate = usePrintQueueStore((state) => state.pinPlate);
  const printPlate = usePrintQueueStore((state) => state.printPlate);
  const printingPlateId = usePrintQueueStore((state) => state.printingPlateId);

  const draggable = canDragPlate(plate) && !mutationPending;
  const isDragging = draggingPlateId === plate.id;
  const showDropBefore = dropTarget?.projectId === projectId && dropTarget.index === plateIndex;
  const showDropAfter = dropTarget?.projectId === projectId && dropTarget.index === plateIndex + 1;

  return (
    <div
      className={`queue-plate-row queue-plate-row--${plate.status}${isDragging ? ' is-dragging' : ''}${draggable ? ' is-draggable' : ''}`}
      data-testid={`queue-plate-${plate.id}`}
      data-queue-row="plate"
      data-project-id={projectId}
      data-plate-index={plateIndex}
      data-status={plate.status}
      onPointerDown={(event) => {
        if (!draggable) return;
        if (event.target instanceof Element && event.target.closest('button')) return;
        beginPointerDrag(event, projectId, plate.id);
      }}
    >
      {showDropBefore && <span className="queue-drop-indicator queue-drop-indicator--before" />}
      <QueueIndexBadge>{`${projectNumber}-${plateIndex + 1}`}</QueueIndexBadge>
      <div className="queue-row-main">
        <QueueThumbnail cover={plate.cover} status={plate.status} />
        <div className="queue-row-copy">
          {/* The suffix is its own element so the name truncates while the
              plate number stays readable, as in the design. It comes from
              plateIndex rather than being parsed back out of the label. */}
          <span className="queue-row-copy__name queue-plate-name">
            <span className="queue-plate-name__base">{plate.name}</span>
            {plate.plateIndex > 0 && (
              <span className="queue-plate-name__suffix">
                {t('-Plate {{index}}', { index: plate.plateIndex })}
              </span>
            )}
          </span>
          {plate.profileName && (
            <span className="queue-row-copy__profile">{plate.profileName}</span>
          )}
          <PlateMeta plate={plate} />
        </div>
      </div>
      <QueueActions
        testIdPrefix={`plate-${plate.id}`}
        deleteEnabled={canDeletePlate(plate)}
        pinEnabled={canPinPlate(plate)}
        pending={mutationPending}
        onDelete={() =>
          requestDelete({
            kind: 'plate',
            projectId,
            plateId: plate.id,
            label: plate.name,
          })
        }
        onPin={() => void pinPlate(projectId, plate.id)}
        printEnabled={canPrintQueuePlate(plate) && printingPlateId === null}
        printPending={printingPlateId === plate.id}
        onPrint={() => void printPlate(plate)}
      />
      {showDropAfter && <span className="queue-drop-indicator queue-drop-indicator--after" />}
    </div>
  );
}

function SinglePlateProjectRow({ project, projectIndex, beginPointerDrag }: ProjectGroupProps) {
  const plate = project.plates[0];
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const draggingPlateId = usePrintQueueStore((state) => state.draggingPlateId);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinProject = usePrintQueueStore((state) => state.pinProject);
  const printPlate = usePrintQueueStore((state) => state.printPlate);
  const printingPlateId = usePrintQueueStore((state) => state.printingPlateId);

  const draggable = canDragPlate(plate) && !mutationPending;
  const isDragging = draggingPlateId === plate.id;
  const showDropBefore = dropTarget?.projectId === project.id && dropTarget.index === 0;

  return (
    <section className="queue-project" data-testid={`queue-project-${project.id}`}>
      <div
        className={`queue-project__header queue-project__header--single${isDragging ? ' is-dragging' : ''}${draggable ? ' is-draggable' : ''}`}
        data-testid={`queue-plate-${plate.id}`}
        data-queue-row="project"
        data-project-id={project.id}
        data-plate-count={project.plates.length}
        data-status={plate.status}
        onPointerDown={(event) => {
          if (!draggable) return;
          if (event.target instanceof Element && event.target.closest('button')) return;
          beginPointerDrag(event, project.id, plate.id);
        }}
      >
        {showDropBefore && <span className="queue-drop-indicator queue-drop-indicator--before" />}
        <QueueIndexBadge>{`${projectIndex + 1}`}</QueueIndexBadge>
        <div className="queue-row-main queue-row-main--project">
          <QueueThumbnail cover={projectCover(project)} status={plate.status} />
          <div className="queue-row-copy">
            <span className="queue-row-copy__name">{project.name}</span>
            {plate.profileName && (
              <span className="queue-row-copy__profile">{plate.profileName}</span>
            )}
            <PlateMeta plate={plate} />
          </div>
        </div>
        <QueueActions
          testIdPrefix={`project-${project.id}`}
          deleteEnabled={canDeleteProject(project)}
          pinEnabled={canPinProject(project)}
          pending={mutationPending}
          onDelete={() =>
            requestDelete({
              kind: 'project',
              projectId: project.id,
              label: project.name,
            })
          }
          onPin={() => void pinProject(project.id)}
          printEnabled={canPrintQueuePlate(plate) && printingPlateId === null}
          printPending={printingPlateId === plate.id}
          onPrint={() => void printPlate(plate)}
        />
      </div>
      <div className="queue-project__divider" />
    </section>
  );
}

interface ProjectGroupProps {
  project: QueueProject;
  projectIndex: number;
  beginPointerDrag: BeginPointerDrag;
}

function ProjectGroup({ project, projectIndex, beginPointerDrag }: ProjectGroupProps) {
  const { t } = useTranslation();
  const expanded = usePrintQueueStore(
    (state) => state.expandedProjects[project.id] ?? true,
  );
  const draggingPlateId = usePrintQueueStore((state) => state.draggingPlateId);
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const toggleProject = usePrintQueueStore((state) => state.toggleProject);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinProject = usePrintQueueStore((state) => state.pinProject);

  const remaining = project.plates.filter(
    (plate) => plate.status === 'pending' || plate.status === 'printing' || plate.status === 'failed',
  ).length;

  return (
    <section className="queue-project" data-testid={`queue-project-${project.id}`}>
      <div
        className="queue-project__header"
        data-queue-row="project"
        data-project-id={project.id}
        data-plate-count={project.plates.length}
      >
        <QueueIndexBadge>{`${projectIndex + 1}`}</QueueIndexBadge>
        <button
          type="button"
          className={`queue-project__chevron${expanded ? ' is-expanded' : ''}`}
          aria-label={expanded ? t('Collapse project') : t('Expand project')}
          data-testid={`project-${project.id}-toggle`}
          onClick={() => toggleProject(project.id)}
        >
          <ChevronIcon />
        </button>
        <div className="queue-row-main queue-row-main--project">
          <QueueThumbnail cover={projectCover(project)} />
          <div className="queue-row-copy queue-row-copy--project">
            <span className="queue-row-copy__name">{project.name}</span>
            <span className="queue-row-copy__summary">
              {/* Two segments split by a slash, mirroring 剩N盘/共M盘 in the
                  design rather than folding both counts into one clause. */}
              {t('{{remaining}} left / {{total}} total', {
                remaining,
                total: project.plates.length,
              })}
            </span>
          </div>
        </div>
        <QueueActions
          testIdPrefix={`project-${project.id}`}
          deleteEnabled={canDeleteProject(project)}
          pinEnabled={canPinProject(project)}
          pending={mutationPending}
          onDelete={() =>
            requestDelete({
              kind: 'project',
              projectId: project.id,
              label: project.name,
            })
          }
          onPin={() => void pinProject(project.id)}
        />
      </div>
      {expanded && (
        <div className="queue-project__plates">
          {project.plates.map((plate, plateIndex) => (
            <PlateRow
              key={plate.id}
              projectId={project.id}
              projectNumber={projectIndex + 1}
              plate={plate}
              plateIndex={plateIndex}
              beginPointerDrag={beginPointerDrag}
            />
          ))}
          {draggingPlateId && (
            <div
              className={`queue-project__drop-end${
                dropTarget?.projectId === project.id &&
                dropTarget.index === project.plates.length
                  ? ' is-active'
                  : ''
              }`}
            />
          )}
        </div>
      )}
      <div className="queue-project__divider" />
    </section>
  );
}

interface DeleteConfirmDialogProps {
  target: DeleteTarget | null;
  pending: boolean;
  onCancel: () => void;
  onConfirm: () => void;
}

function DeleteConfirmDialog({
  target,
  pending,
  onCancel,
  onConfirm,
}: DeleteConfirmDialogProps) {
  const { t } = useTranslation();
  const dialogRef = useRef<HTMLDivElement>(null);
  const cancelRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (!target) return;

    // The WebView shares its focus ring with the wx host, so Tab has to be
    // trapped explicitly or it walks out of the page into native controls.
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && !pending) {
        onCancel();
        return;
      }
      if (event.key !== 'Tab') return;
      const focusable = Array.from(
        dialogRef.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [],
      );
      if (focusable.length === 0) return;
      event.preventDefault();
      const current = focusable.indexOf(document.activeElement as HTMLButtonElement);
      const next = (current + (event.shiftKey ? -1 : 1) + focusable.length) % focusable.length;
      focusable[next]?.focus();
    };

    document.addEventListener('keydown', handleKeyDown);
    cancelRef.current?.focus();
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [onCancel, pending, target]);

  if (!target) return null;
  const isProject = target.kind === 'project';

  return (
    <div className="queue-confirm-overlay" data-testid="queue-delete-confirm-overlay">
      <div
        ref={dialogRef}
        className="queue-confirm"
        role="dialog"
        aria-modal="true"
        aria-labelledby="queue-confirm-title"
      >
        <div className="queue-confirm__title-row">
          <DeleteIcon />
          <span id="queue-confirm-title">
            {isProject ? t('Delete project') : t('Delete plate')}
          </span>
          <button
            type="button"
            className="queue-confirm__close"
            aria-label={t('Close')}
            disabled={pending}
            onClick={onCancel}
          >
            <CloseIcon />
          </button>
        </div>
        <div className="queue-confirm__message">
          {isProject
            ? t('Delete "{{name}}" and all of its plates?', { name: target.label })
            : t('Delete "{{name}}"?', { name: target.label })}
        </div>
        <div className="queue-confirm__footer">
          <button
            ref={cancelRef}
            type="button"
            className="queue-confirm__button queue-confirm__button--cancel"
            data-testid="queue-delete-cancel"
            disabled={pending}
            onClick={onCancel}
          >
            {t('Cancel')}
          </button>
          <button
            type="button"
            className="queue-confirm__button queue-confirm__button--confirm"
            data-testid="queue-delete-confirm"
            disabled={pending}
            onClick={onConfirm}
          >
            {pending ? t('Deleting…') : t('Delete')}
          </button>
        </div>
      </div>
    </div>
  );
}

export function PrintQueuePage() {
  const { t } = useTranslation();
  const scrollRef = useRef<HTMLDivElement>(null);
  const snapshot = usePrintQueueStore((state) => state.snapshot);
  const initialized = usePrintQueueStore((state) => state.initialized);
  const loading = usePrintQueueStore((state) => state.loading);
  const refreshing = usePrintQueueStore((state) => state.refreshing);
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const printingPlateId = usePrintQueueStore((state) => state.printingPlateId);
  const error = usePrintQueueStore((state) => state.error);
  const deleteTarget = usePrintQueueStore((state) => state.deleteTarget);
  const draggingPlateId = usePrintQueueStore((state) => state.draggingPlateId);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const initialize = usePrintQueueStore((state) => state.initialize);
  const cancelDelete = usePrintQueueStore((state) => state.cancelDelete);
  const confirmDelete = usePrintQueueStore((state) => state.confirmDelete);
  const clearError = usePrintQueueStore((state) => state.clearError);
  const beginPointerDrag = usePlateDrag(scrollRef);

  usePrintQueueBridge();

  useEffect(() => {
    void initialize();
  }, [initialize]);

  return (
    <main
      className="print-queue-preview"
      data-testid="print-queue-page"
      data-dragging-plate={draggingPlateId ?? ''}
      data-drop-target={dropTarget ? `${dropTarget.projectId}:${dropTarget.index}` : ''}
      onContextMenu={(event) => event.preventDefault()}
    >
      <section className="print-queue-panel" aria-label={t('Task list')}>
        <header className="print-queue-header">
          <span className="print-queue-header__title">{t('Task list')}</span>
        </header>

        <div className="print-queue-body">
          <div ref={scrollRef} className="print-queue-scroll" data-testid="print-queue-scroll">
            <div className="print-queue-list">
              {!initialized && loading ? (
                <div className="print-queue-state">{t('Loading the task list…')}</div>
              ) : snapshot.projects.length === 0 ? (
                <div className="print-queue-state">{t('No print tasks')}</div>
              ) : (
                snapshot.projects.map((project, projectIndex) => {
                  const Row = isSinglePlateProject(project) ? SinglePlateProjectRow : ProjectGroup;
                  return (
                    <Row
                      key={project.id}
                      project={project}
                      projectIndex={projectIndex}
                      beginPointerDrag={beginPointerDrag}
                    />
                  );
                })
              )}
            </div>
          </div>
        </div>

        {(refreshing || mutationPending || printingPlateId !== null) && (
          <div className="print-queue-busy" aria-live="polite">
            <span />
          </div>
        )}
      </section>

      {error && (
        <button
          type="button"
          className="print-queue-error"
          data-testid="print-queue-error"
          onClick={clearError}
        >
          {t(error)}
        </button>
      )}

      <DeleteConfirmDialog
        target={deleteTarget}
        pending={mutationPending}
        onCancel={cancelDelete}
        onConfirm={() => void confirmDelete()}
      />
    </main>
  );
}
