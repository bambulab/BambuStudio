import { useCallback, useEffect, useMemo, useRef } from 'react';
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
import type {
  DeleteTarget,
  QueueDragSource,
  QueueDropTarget,
  QueuePlate,
  QueueProject,
  QueueProjectStatus,
} from './types';
import {
  canPrintQueuePlate,
  formatCostTime,
  formatWeight,
  isSinglePlateProject,
  projectCover,
  projectStatus,
} from './types';
import {
  canDeletePlate,
  canDeleteProject,
  canDragPlate,
  canDragProject,
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
  const deleteLabel = t('Delete');
  const pinLabel = t('Pin to top');
  const printLabel = printPending ? t('Preparing...') : t('Print');
  // The hint is drawn from data-tip by the wrapper instead of being a title on the
  // button. A native tooltip is only requested off a mouse move, so a cursor that
  // comes to rest inside an icon without moving again never gets one, and a
  // disabled button is skipped outright; :hover depends on neither.
  return (
    <div className="queue-actions" data-testid={`${testIdPrefix}-actions`}>
      <span className="queue-action-slot" data-tip={deleteLabel}>
        <button
          type="button"
          className="queue-action queue-action--delete"
          data-testid={`${testIdPrefix}-delete`}
          aria-label={deleteLabel}
          disabled={!deleteEnabled || pending}
          onClick={(event) => {
            event.stopPropagation();
            onDelete();
          }}
        >
          <DeleteIcon />
        </button>
      </span>
      <span className="queue-action-slot" data-tip={pinLabel}>
        <button
          type="button"
          className="queue-action queue-action--pin"
          data-testid={`${testIdPrefix}-pin`}
          aria-label={pinLabel}
          disabled={!pinEnabled || pending}
          onClick={(event) => {
            event.stopPropagation();
            onPin();
          }}
        >
          <PinIcon />
        </button>
      </span>
      <span className="queue-action-slot" data-tip={printLabel}>
        <button
          type="button"
          className="queue-action queue-action--print"
          data-testid={`${testIdPrefix}-print`}
          aria-label={printLabel}
          disabled={!onPrint || !printEnabled || pending || printPending}
          onClick={(event) => {
            event.stopPropagation();
            onPrint?.();
          }}
        >
          <PrintIcon />
        </button>
      </span>
    </div>
  );
}

const DRAG_START_THRESHOLD_PX = 4;
const DRAG_AUTO_SCROLL_EDGE_PX = 48;
const DRAG_AUTO_SCROLL_MAX_STEP_PX = 18;
const DRAG_EXPAND_DELAY_MS = 450;
// Band along the top and bottom of a project where the drop lands between
// projects instead of inside one.
const DROP_PROJECT_EDGE_PX = 10;

type BeginPointerDrag = (
  event: ReactPointerEvent<HTMLElement>,
  source: QueueDragSource,
) => void;

interface ActiveDrag {
  pointerId: number;
  source: QueueDragSource;
  originY: number;
  pointerY: number;
  started: boolean;
  hoverProjectId: string | null;
  hoverSince: number;
}

// A whole multi-plate project can only be reordered between projects. A plate,
// or the single plate a one-plate project consists of, can also land inside
// another project - but not inside the project it already belongs to.
function acceptsPlateDrop(source: QueueDragSource, projectId: string): boolean {
  if (source.kind === 'plate') return true;
  return source.single && source.projectId !== projectId;
}

function resolvePlateSlot(
  section: HTMLElement,
  clientY: number,
  projectId: string,
): QueueDropTarget {
  const rows = Array.from(section.querySelectorAll<HTMLElement>('[data-queue-row="plate"]'));

  for (const row of rows) {
    const rect = row.getBoundingClientRect();
    if (rect.height === 0) continue;
    const plateIndex = Number(row.dataset.plateIndex ?? '0');
    if (clientY <= rect.bottom) {
      const insertAfter = clientY >= rect.top + rect.height / 2;
      return { kind: 'plate', projectId, index: plateIndex + (insertAfter ? 1 : 0) };
    }
  }

  // Collapsed and single-plate projects draw no plate rows, so there is no
  // visible slot to aim at and the plate joins the project at the end.
  return { kind: 'plate', projectId, index: Number(section.dataset.plateCount ?? '0') };
}

// Hit-test the rendered rows instead of relying on per-row dragover events: the
// pointer may sit still while the list auto-scrolls underneath it, and only a
// geometry query stays correct in that case.
function resolveDropTarget(
  container: HTMLElement,
  clientY: number,
  source: QueueDragSource,
): QueueDropTarget | null {
  const sections = Array.from(container.querySelectorAll<HTMLElement>('[data-queue-section]'));
  let last: QueueDropTarget | null = null;

  for (const section of sections) {
    const rect = section.getBoundingClientRect();
    const projectId = section.dataset.projectId;
    if (rect.height === 0 || !projectId) continue;

    const projectIndex = Number(section.dataset.projectIndex ?? '0');
    const before: QueueDropTarget = { kind: 'project', index: projectIndex };
    const after: QueueDropTarget = { kind: 'project', index: projectIndex + 1 };

    if (clientY > rect.bottom) {
      last = after;
      continue;
    }
    // Without an inner drop the row splits in half, so every pixel of it aims
    // at one of the two gaps around it.
    if (!acceptsPlateDrop(source, projectId)) {
      return clientY < rect.top + rect.height / 2 ? before : after;
    }
    if (clientY < rect.top + DROP_PROJECT_EDGE_PX) return before;
    if (clientY > rect.bottom - DROP_PROJECT_EDGE_PX) return after;
    return resolvePlateSlot(section, clientY, projectId);
  }

  return last;
}

function autoScrollStep(distanceIntoEdge: number): number {
  return Math.min(DRAG_AUTO_SCROLL_MAX_STEP_PX, 2 + distanceIntoEdge / 4);
}

function useQueueDrag(scrollRef: RefObject<HTMLDivElement | null>): BeginPointerDrag {
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

    const target = resolveDropTarget(container, clientY, drag.source);
    setDropTarget(target);

    // Hovering inside a collapsed project long enough opens it, so the plate can
    // be aimed at a precise slot instead of only joining at the end.
    const hovered = target?.kind === 'plate' ? target.projectId : null;
    if (hovered !== drag.hoverProjectId) {
      drag.hoverProjectId = hovered;
      drag.hoverSince = performance.now();
      return;
    }
    if (hovered && performance.now() - drag.hoverSince >= DRAG_EXPAND_DELAY_MS) {
      expandProject(hovered);
    }
  }, [expandProject, scrollRef, setDropTarget]);

  return useCallback<BeginPointerDrag>((event, source) => {
    if (event.button !== 0 || dragRef.current) return;
    const container = scrollRef.current;
    if (!container) return;

    dragRef.current = {
      pointerId: event.pointerId,
      source,
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
        beginDrag(drag.source);
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
  const dragSource = usePrintQueueStore((state) => state.dragSource);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinPlate = usePrintQueueStore((state) => state.pinPlate);
  const printPlate = usePrintQueueStore((state) => state.printPlate);
  const printingPlateId = usePrintQueueStore((state) => state.printingPlateId);

  const draggable = canDragPlate(plate) && !mutationPending;
  const isDragging = dragSource?.kind === 'plate' && dragSource.plateId === plate.id;
  const plateSlot = dropTarget?.kind === 'plate' && dropTarget.projectId === projectId
    ? dropTarget.index
    : null;
  const showDropBefore = plateSlot === plateIndex;
  const showDropAfter = plateSlot === plateIndex + 1;

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
        beginPointerDrag(event, { kind: 'plate', projectId, plateId: plate.id });
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
            <span className="queue-plate-name__base" title={plate.name}>{plate.name}</span>
            {plate.plateIndex > 0 && (
              <span className="queue-plate-name__suffix">
                {t('-Plate {{index}}', { index: plate.plateIndex })}
              </span>
            )}
          </span>
          {plate.profileName && (
            <span className="queue-row-copy__profile" title={plate.profileName}>{plate.profileName}</span>
          )}
          <PlateMeta plate={plate} />
        </div>
      </div>
      <QueueActions
        testIdPrefix={`plate-${plate.id}`}
        deleteEnabled={canDeletePlate(plate)}
        pinEnabled={canPinPlate(plate, plateIndex)}
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

// The slot between two projects is drawn by the project that follows it, so only
// the trailing slot after the last project needs a line of its own.
function ProjectDropIndicator({
  projectIndex,
  projectCount,
}: {
  projectIndex: number;
  projectCount: number;
}) {
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  if (dropTarget?.kind !== 'project') return null;
  if (dropTarget.index === projectIndex) {
    return <span className="queue-drop-indicator queue-drop-indicator--project queue-drop-indicator--before" />;
  }
  if (dropTarget.index === projectIndex + 1 && projectIndex === projectCount - 1) {
    return <span className="queue-drop-indicator queue-drop-indicator--project queue-drop-indicator--after" />;
  }
  return null;
}

function SinglePlateProjectRow({ project, projectIndex, projectCount, beginPointerDrag }: ProjectRowProps) {
  const plate = project.plates[0];
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const dragSource = usePrintQueueStore((state) => state.dragSource);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinProject = usePrintQueueStore((state) => state.pinProject);
  const printPlate = usePrintQueueStore((state) => state.printPlate);
  const printingPlateId = usePrintQueueStore((state) => state.printingPlateId);

  const draggable = canDragProject(project) && !mutationPending;
  const isDragging = dragSource?.kind === 'project' && dragSource.projectId === project.id;
  // A plate aimed at this row joins the project. There is no plate row to draw a
  // line against, so the row as a whole marks itself as the target.
  const isDropInto = dropTarget?.kind === 'plate' && dropTarget.projectId === project.id;

  return (
    <section
      className={`queue-project${isDropInto ? ' is-drop-into' : ''}`}
      data-testid={`queue-project-${project.id}`}
      data-queue-section=""
      data-project-id={project.id}
      data-project-index={projectIndex}
      data-plate-count={project.plates.length}
    >
      <ProjectDropIndicator projectIndex={projectIndex} projectCount={projectCount} />
      <div
        className={`queue-project__header queue-project__header--single${isDragging ? ' is-dragging' : ''}${draggable ? ' is-draggable' : ''}`}
        data-testid={`queue-plate-${plate.id}`}
        data-status={plate.status}
        onPointerDown={(event) => {
          if (!draggable) return;
          if (event.target instanceof Element && event.target.closest('button')) return;
          beginPointerDrag(event, {
            kind: 'project',
            projectId: project.id,
            single: true,
            plateId: plate.id,
          });
        }}
      >
        <QueueIndexBadge>{`${projectIndex + 1}`}</QueueIndexBadge>
        <div className="queue-row-main queue-row-main--project">
          <QueueThumbnail cover={projectCover(project)} status={plate.status} />
          <div className="queue-row-copy">
            <span className="queue-row-copy__name" title={project.name}>{project.name}</span>
            {plate.profileName && (
              <span className="queue-row-copy__profile" title={plate.profileName}>{plate.profileName}</span>
            )}
            <PlateMeta plate={plate} />
          </div>
        </div>
        <QueueActions
          testIdPrefix={`project-${project.id}`}
          deleteEnabled={canDeleteProject(project)}
          pinEnabled={canPinProject(project, projectIndex)}
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

interface ProjectRowProps {
  project: QueueProject;
  projectIndex: number;
  projectCount: number;
  beginPointerDrag: BeginPointerDrag;
}

function ProjectGroup({ project, projectIndex, projectCount, beginPointerDrag }: ProjectRowProps) {
  const { t } = useTranslation();
  const expanded = usePrintQueueStore(
    (state) => state.expandedProjects[project.id] ?? true,
  );
  const dragSource = usePrintQueueStore((state) => state.dragSource);
  const mutationPending = usePrintQueueStore((state) => state.mutationPending);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const toggleProject = usePrintQueueStore((state) => state.toggleProject);
  const requestDelete = usePrintQueueStore((state) => state.requestDelete);
  const pinProject = usePrintQueueStore((state) => state.pinProject);

  const remaining = project.plates.filter(
    (plate) => plate.status === 'pending' || plate.status === 'printing' || plate.status === 'failed',
  ).length;

  const draggable = canDragProject(project) && !mutationPending;
  const isDragging = dragSource?.kind === 'project' && dragSource.projectId === project.id;
  // While collapsed the project has no plate rows to draw a line against, so it
  // marks itself as the target of an incoming plate.
  const isDropInto = !expanded && dropTarget?.kind === 'plate' && dropTarget.projectId === project.id;

  return (
    <section
      className={`queue-project${isDropInto ? ' is-drop-into' : ''}`}
      data-testid={`queue-project-${project.id}`}
      data-queue-section=""
      data-project-id={project.id}
      data-project-index={projectIndex}
      data-plate-count={project.plates.length}
    >
      <ProjectDropIndicator projectIndex={projectIndex} projectCount={projectCount} />
      <div
        className={`queue-project__header${isDragging ? ' is-dragging' : ''}${draggable ? ' is-draggable' : ''}`}
        onPointerDown={(event) => {
          if (!draggable) return;
          if (event.target instanceof Element && event.target.closest('button')) return;
          beginPointerDrag(event, {
            kind: 'project',
            projectId: project.id,
            single: false,
            plateId: null,
          });
        }}
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
            <span className="queue-row-copy__name" title={project.name}>{project.name}</span>
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
          pinEnabled={canPinProject(project, projectIndex)}
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
          {dragSource && (
            <div
              className={`queue-project__drop-end${
                dropTarget?.kind === 'plate' &&
                dropTarget.projectId === project.id &&
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

function StatusFilter() {
  const { t } = useTranslation();
  const statusFilter = usePrintQueueStore((state) => state.statusFilter);
  const setStatusFilter = usePrintQueueStore((state) => state.setStatusFilter);

  return (
    <span className="print-queue-filter">
      <select
        className="print-queue-filter__select"
        data-testid="print-queue-filter"
        aria-label={t('Filter by status')}
        value={statusFilter}
        onChange={(event) => setStatusFilter(event.target.value as QueueProjectStatus)}
      >
        {/* Labels are spelled out per option for the same reason as the plate
            status ribbon: i18next-parser only extracts literal t() arguments. */}
        <option value="in-progress">{t('In progress')}</option>
        <option value="completed">{t('Completed')}</option>
      </select>
      <ChevronIcon className="print-queue-filter__caret" />
    </span>
  );
}

// Exposed on the page root so the WebView E2E suite can assert on drag state
// without reaching into React internals.
function describeDragSource(source: QueueDragSource | null): string {
  if (!source) return '';
  return source.kind === 'plate' ? `plate:${source.plateId}` : `project:${source.projectId}`;
}

function describeDropTarget(target: QueueDropTarget | null): string {
  if (!target) return '';
  return target.kind === 'project'
    ? `project:${target.index}`
    : `plate:${target.projectId}:${target.index}`;
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
  const statusFilter = usePrintQueueStore((state) => state.statusFilter);
  const error = usePrintQueueStore((state) => state.error);
  const deleteTarget = usePrintQueueStore((state) => state.deleteTarget);
  const dragSource = usePrintQueueStore((state) => state.dragSource);
  const dropTarget = usePrintQueueStore((state) => state.dropTarget);
  const initialize = usePrintQueueStore((state) => state.initialize);
  const cancelDelete = usePrintQueueStore((state) => state.cancelDelete);
  const confirmDelete = usePrintQueueStore((state) => state.confirmDelete);
  const clearError = usePrintQueueStore((state) => state.clearError);
  const beginPointerDrag = useQueueDrag(scrollRef);

  // Each row keeps the position it holds in the queue, not in the filtered view:
  // the index badge, Pin to top and every drop slot are all expressed against the
  // full snapshot, and renumbering them per view would move the wrong rows.
  const visibleProjects = useMemo(
    () => snapshot.projects
      .map((project, projectIndex) => ({ project, projectIndex }))
      .filter(({ project }) => projectStatus(project) === statusFilter),
    [snapshot.projects, statusFilter],
  );

  usePrintQueueBridge();

  useEffect(() => {
    void initialize();
  }, [initialize]);

  return (
    <main
      className="print-queue-preview"
      data-testid="print-queue-page"
      data-drag-source={describeDragSource(dragSource)}
      data-drop-target={describeDropTarget(dropTarget)}
      onContextMenu={(event) => event.preventDefault()}
    >
      <section className="print-queue-panel" aria-label={t('Task list')}>
        <header className="print-queue-header">
          <span className="print-queue-header__title">{t('Task list')}</span>
          <StatusFilter />
        </header>

        <div className="print-queue-body">
          <div ref={scrollRef} className="print-queue-scroll" data-testid="print-queue-scroll">
            <div className="print-queue-list">
              {!initialized && loading ? (
                <div className="print-queue-state">{t('Loading the task list…')}</div>
              ) : snapshot.projects.length === 0 ? (
                <div className="print-queue-state">{t('No print tasks')}</div>
              ) : visibleProjects.length === 0 ? (
                <div className="print-queue-state">{t('No tasks match this filter')}</div>
              ) : (
                visibleProjects.map(({ project, projectIndex }) => {
                  const Row = isSinglePlateProject(project) ? SinglePlateProjectRow : ProjectGroup;
                  return (
                    <Row
                      key={project.id}
                      project={project}
                      projectIndex={projectIndex}
                      projectCount={snapshot.projects.length}
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
