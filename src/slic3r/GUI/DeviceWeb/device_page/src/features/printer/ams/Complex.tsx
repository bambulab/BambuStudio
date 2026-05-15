import { useRef } from "react";

export default function Complex() {

    const refs = useRef<(HTMLDivElement | null)[]>([]);


    const colors = ["red"];
    return (<div>

        {
            // 构造多个子控件，并持有引用
            colors.map((color, index) => (
                <div key={color} ref={(e) => { refs.current[index] = e }}></div>
            ))

        }

    </div>);

}