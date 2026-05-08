export function TempInput() {
    return (
        <div className="flex flex-row w-[8rem]">
            <p>123</p>
            <p>℃</p>
            <p>/</p>
            <input type="number" placeholder="temp" className="w-[3rem]"/>
            <p>℃</p>
        </div>
    );
}