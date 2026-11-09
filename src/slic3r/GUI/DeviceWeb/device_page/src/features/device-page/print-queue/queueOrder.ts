import type {
  PrintQueueSnapshot,
  QueueDragSource,
  QueueDropTarget,
  QueuePlate,
} from './types';

// Ordering on the wire is expressed as "put me after this sibling", where 0
// means "move to the top" (see PUT .../sort in the queue API).
export const PREV_SIBLING_TOP = '0';

interface Slot {
  prevId: string;
  // Slot in the list with the dragged entry taken out.
  index: number;
  // -1 when the dragged entry is not part of this list.
  fromIndex: number;
}

// A rendered slot index still counts the dragged row, because the row keeps its
// place in the list while it is being dragged. When the entry moves down inside
// its own list the rendered slot therefore sits one past the gap the user is
// aiming at, and the sibling taken from the shortened list would be one too far.
function resolveSlot<T extends { id: string }>(
  items: readonly T[],
  draggedId: string,
  renderIndex: number,
): Slot {
  const fromIndex = items.findIndex((item) => item.id === draggedId);
  const shifted = fromIndex >= 0 && fromIndex < renderIndex ? renderIndex - 1 : renderIndex;
  const remaining = fromIndex >= 0 ? items.filter((item) => item.id !== draggedId) : items;
  const index = Math.max(0, Math.min(shifted, remaining.length));
  return {
    prevId: index === 0 ? PREV_SIBLING_TOP : remaining[index - 1]?.id ?? PREV_SIBLING_TOP,
    index,
    fromIndex,
  };
}

// Extracting a plate leaves the project it came from in place, so that slot
// refers to the list as rendered and needs no compensation.
function prevSiblingAt<T extends { id: string }>(items: readonly T[], renderIndex: number): string {
  const index = Math.max(0, Math.min(renderIndex, items.length));
  return index === 0 ? PREV_SIBLING_TOP : items[index - 1]?.id ?? PREV_SIBLING_TOP;
}

function findPlate(
  snapshot: PrintQueueSnapshot,
  projectId: string,
  plateId: string,
): QueuePlate | null {
  const project = snapshot.projects.find((candidate) => candidate.id === projectId);
  return project?.plates.find((plate) => plate.id === plateId) ?? null;
}

export type QueueMoveCommand =
  | { kind: 'moveProject'; projectId: string; prevProjectId: string }
  | { kind: 'movePlate'; plateId: string; targetProjectId: string; prevPlateId: string }
  | { kind: 'extractPlate'; plateId: string; title: string; cover: string; prevProjectId: string };

// Maps a finished drag onto the one queue mutation it stands for, or null when
// the drop would not change anything. The three outcomes are:
//   project -> between projects        reorder the queue
//   plate   -> inside another project  move the plate into it
//   plate   -> between projects        extract the plate into its own project
export function resolveMoveCommand(
  snapshot: PrintQueueSnapshot,
  source: QueueDragSource,
  target: QueueDropTarget,
): QueueMoveCommand | null {
  if (target.kind === 'project') {
    if (source.kind === 'plate') {
      const plate = findPlate(snapshot, source.projectId, source.plateId);
      if (!plate) return null;
      return {
        kind: 'extractPlate',
        plateId: source.plateId,
        title: plate.name,
        cover: plate.cover,
        prevProjectId: prevSiblingAt(snapshot.projects, target.index),
      };
    }

    const slot = resolveSlot(snapshot.projects, source.projectId, target.index);
    if (slot.index === slot.fromIndex) return null;
    return { kind: 'moveProject', projectId: source.projectId, prevProjectId: slot.prevId };
  }

  // Only a plate can end up inside a project. A multi-plate project has no such
  // move, and a single-plate project cannot be dropped into itself.
  const plateId = source.kind === 'plate'
    ? source.plateId
    : (source.single && source.projectId !== target.projectId ? source.plateId : null);
  if (!plateId) return null;

  const targetProject = snapshot.projects.find((project) => project.id === target.projectId);
  if (!targetProject) return null;

  const slot = resolveSlot(targetProject.plates, plateId, target.index);
  if (slot.index === slot.fromIndex) return null;
  return {
    kind: 'movePlate',
    plateId,
    targetProjectId: target.projectId,
    prevPlateId: slot.prevId,
  };
}
