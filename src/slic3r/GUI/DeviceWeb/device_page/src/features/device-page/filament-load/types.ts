export type FilamentLoadStepStatus = 'pending' | 'current' | 'done';
export type FilamentLoadStepKind = 'load' | 'unload' | 'vt_load';

export interface FilamentLoadActionControl {
  visible: boolean;
  enabled: boolean;
  text: string;
  disabled_reason: string;
}

export interface FilamentLoadStepItem {
  text: string;
  status: FilamentLoadStepStatus;
}

export interface FilamentLoadStepArea {
  kind: FilamentLoadStepKind;
  slot_text: string;
  items: FilamentLoadStepItem[];
}

export interface FilamentLoadActions {
  retry: FilamentLoadActionControl;
  stop: FilamentLoadActionControl;
}

export interface FilamentLoadDisplay {
  visible: boolean;
  expanded: boolean;
  title: string;
  image_name: string;
  steps: FilamentLoadStepArea;
  actions: FilamentLoadActions;
}

export interface FilamentLoadState {
  display: FilamentLoadDisplay;
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message: string;
  payload?: T;
}
