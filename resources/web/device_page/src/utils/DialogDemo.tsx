"use client";
// ModelessDialogDemo.tsx — only closes via explicit buttons
// Dependences: pnpm add @radix-ui/react-dialog zustand
// Styling: TailwindCSS

import * as Dialog from "@radix-ui/react-dialog";
import { create } from "zustand";

/** ------------------------------------------------------------------
 * Zustand store
 * ------------------------------------------------------------------*/
interface DialogState {
  open: boolean;
  setOpen: (next: boolean) => void;
}

export const useDialogStore = create<DialogState>((set) => ({
  open: false,
  setOpen: (next) => set({ open: next }),
}));

/** ------------------------------------------------------------------
 * Button that only **opens** the dialog; cannot close it.
 * ------------------------------------------------------------------*/
export function OpenDialogButton({ className }: { className?: string }) {
  const { setOpen } = useDialogStore();
  return (
    <button
      type="button"
      onClick={() => setOpen(true)}
      className={`px-4 py-2 border rounded-md text-sm font-medium shadow-sm hover:bg-gray-50 ${className ?? ""}`}
    >
      Show dialog
    </button>
  );
}

/** ------------------------------------------------------------------
 * The non‑modal dialog: remains open on outside click / Esc.
 * ------------------------------------------------------------------*/
export default function ModelessDialogDemo() {
  const { open, setOpen } = useDialogStore();

  return (
    <>
      {/* Anywhere in the tree */}
      <OpenDialogButton />

      <Dialog.Root open={open} onOpenChange={setOpen} modal={false}>
        <Dialog.Portal>
          <Dialog.Content
            /* prevent escape key or outside click from closing */
            onInteractOutside={(e) => e.preventDefault()}
            onEscapeKeyDown={(e) => e.preventDefault()}
            className="fixed top-1/2 left-1/2 z-50 w-[24rem] -translate-x-1/2 -translate-y-1/2 rounded-2xl bg-white p-6 shadow-2xl focus:outline-none"
          >
            <header className="flex items-start justify-between">
              <Dialog.Title className="text-xl font-semibold">
                Non‑modal dialog
              </Dialog.Title>
              <Dialog.Close asChild /* explicit close button */>
                <button
                  type="button"
                  aria-label="Close"
                  className="h-8 w-8 flex items-center justify-center rounded-full text-xl leading-none hover:bg-gray-100"
                >
                  ×
                </button>
              </Dialog.Close>
            </header>

            <Dialog.Description className="mt-2 text-sm text-gray-500">
              Outside clicks &amp; ESC are ignored — only the × or “Got it” close
              this dialog.
            </Dialog.Description>

            <div className="mt-6 flex justify-end gap-2">
              <Dialog.Close asChild>
                <button
                  type="button"
                  className="px-4 py-2 text-sm font-medium border rounded-md hover:bg-gray-50"
                >
                  Got it
                </button>
              </Dialog.Close>
            </div>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </>
  );
}
