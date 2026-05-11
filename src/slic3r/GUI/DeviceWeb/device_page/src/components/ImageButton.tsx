import React from "react";
import Image from "./Image";

interface IconButtonProps {
    icon: string;
    hover?: string;
    active?: string;
    className?: string;
}


export default function ImageButton(props: IconButtonProps) {
    const [hovered, setHovered] = React.useState(false);
    const [active, setActive] = React.useState(false);

    let icon = props.icon;
    if (active && props.active) {
        icon = props.active;
    } else if (hovered && props.hover) {
        icon = props.hover;
    }

    return (
        <button className={props.className}
            onMouseEnter={() => setHovered(true)}
            onMouseLeave={() => setHovered(false)}
            onClick={() => setActive(!active)}>

            <Image src={icon} />

        </button>
    );
}