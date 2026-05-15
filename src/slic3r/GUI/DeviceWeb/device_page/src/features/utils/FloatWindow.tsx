import * as Popover from "@radix-ui/react-popover";
import type { ReactNode } from "react";
import { useState } from "react";

export default function FilamentPopover({ trigger }: { trigger?: ReactNode }) {
    const [open, setOpen] = useState(false);

    return (
        <Popover.Root open={open} onOpenChange={(v) => setOpen(v)}>
            <Popover.Trigger asChild>
                {trigger || (
                    <button onClick={() => setOpen(true)} className="px-[1rem] py-[0.5rem] bg-blue-600 text-white rounded-[8rem]">
                        打开浮层
                    </button>
                )}
            </Popover.Trigger>

            <Popover.Portal>
                <Popover.Content side="bottom" sideOffset={12} className="z-50 w-[35rem] max-w-[90vw] bg-white rounded-[1.5rem] shadow-xl border p-[1.5rem] flex gap-[1.5rem] relative"
                    // 禁止自动关闭
                    onPointerDownOutside={(e) => e.preventDefault()}
                    onFocusOutside={(e) => e.preventDefault()}
                >
                    {/* 弹窗内容同前 */}
                    <p className="text-body">popover</p>

                    {/* 关闭按钮 */}
                    <Popover.Close asChild>
                        <button
                            className="absolute top-[0.75rem] right-[0.75rem] text-gray-400 hover:text-gray-700 transition"
                            aria-label="关闭"
                            tabIndex={0}
                            onClick={() => setOpen(false)}
                        >
                            <svg width="1.5rem" height="1.5rem" fill="none" viewBox="0 0 24 24">
                                <path
                                    stroke="currentColor"
                                    strokeWidth="2"
                                    d="M6 6l12 12M6 18L18 6"
                                />
                            </svg>
                        </button>
                    </Popover.Close>
                </Popover.Content>
            </Popover.Portal>
        </Popover.Root>
    );
}
