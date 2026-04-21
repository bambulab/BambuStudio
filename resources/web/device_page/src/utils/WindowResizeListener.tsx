function setRemBase() {
    const rootFontSize = 16; // root font size in pixels
    document.documentElement.style.fontSize = window.devicePixelRatio*rootFontSize + 'px';
}



export default function WindowResizeListener(){
    window.addEventListener('resize', setRemBase);
    document.addEventListener('DOMContentLoaded', setRemBase);

    return null;
}