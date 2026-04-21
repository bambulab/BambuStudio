import React from "react";


type Props = Omit<React.ImgHTMLAttributes<HTMLImageElement>, "src"> & {
    src: string;
};

export default function Image({ src, ...rest }: Props) {
    const finalSrc = "/img/" + src;
    return <img src={finalSrc} {...rest} />;
}
