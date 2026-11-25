import type {
  FilamentLoadActionControl,
  FilamentLoadDisplay,
  FilamentLoadState,
  FilamentLoadStepArea,
  FilamentLoadStepItem,
  FilamentLoadStepKind,
  FilamentLoadStepStatus,
} from './types';

const stepStatuses: FilamentLoadStepStatus[] = ['pending', 'current', 'done'];
const stepKinds: FilamentLoadStepKind[] = ['load', 'unload', 'vt_load'];

function boolValue(value: unknown) {
  return typeof value === 'boolean' ? value : false;
}

function stringValue(value: unknown) {
  return typeof value === 'string' ? value : '';
}

function stepStatus(value: unknown): FilamentLoadStepStatus {
  return stepStatuses.includes(value as FilamentLoadStepStatus)
    ? value as FilamentLoadStepStatus
    : 'pending';
}

function stepKind(value: unknown): FilamentLoadStepKind {
  return stepKinds.includes(value as FilamentLoadStepKind)
    ? value as FilamentLoadStepKind
    : 'load';
}

function normalizeAction(value: unknown): FilamentLoadActionControl {
  const source = value && typeof value === 'object' ? value as Partial<FilamentLoadActionControl> : {};
  return {
    visible: boolValue(source.visible),
    enabled: boolValue(source.enabled),
    text: stringValue(source.text),
    disabled_reason: stringValue(source.disabled_reason),
  };
}

function normalizeStep(value: unknown): FilamentLoadStepItem {
  const source = value && typeof value === 'object' ? value as Partial<FilamentLoadStepItem> : {};
  return {
    text: stringValue(source.text),
    status: stepStatus(source.status),
  };
}

function normalizeSteps(value: unknown): FilamentLoadStepArea {
  const source = value && typeof value === 'object' ? value as Partial<FilamentLoadStepArea> : {};
  const items = Array.isArray(source.items) ? source.items.map(normalizeStep).filter((item) => item.text) : [];
  return {
    kind: stepKind(source.kind),
    slot_text: stringValue(source.slot_text),
    items,
  };
}

function normalizeDisplay(value: unknown): FilamentLoadDisplay {
  const source = value && typeof value === 'object' ? value as Partial<FilamentLoadDisplay> : {};
  const actions = source.actions && typeof source.actions === 'object' ? source.actions : {};
  return {
    visible: boolValue(source.visible),
    expanded: boolValue(source.expanded),
    title: stringValue(source.title),
    image_name: stringValue(source.image_name),
    steps: normalizeSteps(source.steps),
    actions: {
      retry: normalizeAction((actions as Partial<FilamentLoadDisplay['actions']>).retry),
      stop: normalizeAction((actions as Partial<FilamentLoadDisplay['actions']>).stop),
    },
  };
}

export const emptyFilamentLoadState: FilamentLoadState = {
  display: {
    visible: false,
    expanded: false,
    title: 'Filament loading...',
    image_name: '',
    steps: {
      kind: 'load',
      slot_text: '',
      items: [],
    },
    actions: {
      retry: { visible: false, enabled: false, text: 'Retry', disabled_reason: '' },
      stop: { visible: false, enabled: false, text: 'Stop', disabled_reason: '' },
    },
  },
};

export function normalizeState(value: Partial<FilamentLoadState> | undefined): FilamentLoadState {
  return {
    display: normalizeDisplay(value?.display),
  };
}
