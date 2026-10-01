export function CenterRelativePosition(origin: React.RefObject<HTMLDivElement | null>, target: React.RefObject<HTMLDivElement | null>): [number, number] {
    if (!origin.current || !target.current) return [0, 0];

    const originRect = origin.current.getBoundingClientRect();
    const targetRect = target.current.getBoundingClientRect();

    const dx = (targetRect.left + targetRect.width / 2) - originRect.left;
    const dy = (targetRect.top + targetRect.height / 2) - originRect.top;

    return [dx, dy];
}

export function LeftCenterRelativePosition(origin: React.RefObject<HTMLDivElement | null>, target: React.RefObject<HTMLDivElement | null>): [number, number] {
    if (!origin.current || !target.current) return [0, 0];

    const originRect = origin.current.getBoundingClientRect();
    const targetRect = target.current.getBoundingClientRect();

    const dx = (targetRect.left) - originRect.left;
    const dy = (targetRect.top + targetRect.height / 2) - originRect.top;

    return [dx, dy];
}

export function RightCenterRelativePosition(origin: React.RefObject<HTMLDivElement | null>, target: React.RefObject<HTMLDivElement | null>): [number, number] {
    if (!origin.current || !target.current) return [0, 0];

    const originRect = origin.current.getBoundingClientRect();
    const targetRect = target.current.getBoundingClientRect();

    const dx = (targetRect.left + targetRect.width) - originRect.left;
    const dy = (targetRect.top + targetRect.height / 2) - originRect.top;

    return [dx, dy];
}

export function TopCenterRelativePosition(origin: React.RefObject<HTMLDivElement | null>, target: React.RefObject<HTMLDivElement | null>): [number, number] {
    if (!origin.current || !target.current) return [0, 0];

    const originRect = origin.current.getBoundingClientRect();
    const targetRect = target.current.getBoundingClientRect();

    const dx = (targetRect.left + targetRect.width / 2) - originRect.left;
    const dy = (targetRect.top) - originRect.top;

    return [dx, dy];
}

export function BottumCenterRelativePosition(origin: React.RefObject<HTMLDivElement | null>, target: React.RefObject<HTMLDivElement | null>): [number, number] {
    if (!origin.current || !target.current) return [0, 0];

    const originRect = origin.current.getBoundingClientRect();
    const targetRect = target.current.getBoundingClientRect();

    const dx = (targetRect.left + targetRect.width / 2) - originRect.left;
    const dy = (targetRect.top + targetRect.height) - originRect.top;

    return [dx, dy];
}