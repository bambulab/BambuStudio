const Card = ({ className = "" }) => {
  return (
    <div className={`relative w-[58rem] h-[92rem] bg-blue-300 ${className}`}>
      {/* 左侧灰色竖条 */}
      <div className="absolute left-0 top-[10rem] w-[5rem] h-[80rem] bg-gray-200 rounded-[2.5rem]" />

      {/* 右侧灰色竖条 */}
      <div className="absolute right-0 top-[10rem] w-[5rem] h-[80rem] bg-gray-200 rounded-[2.5rem]" />

      {/* 背景灰色卡片 */}
      <div className="absolute left-[5rem] right-[5rem] top-[18rem] bottom-[10rem] bg-gray-200 border-gray-200 z-0" />

      {/* 主体红色卡片 */}
      <div className="absolute left-[5rem] right-[5rem] top-[20rem] h-[60rem] bg-red-300 border border-gray-200" />

      {/* 顶部灰色横条 */}
      <div className="absolute left-[8rem] right-[8rem] top-0 h-[4rem] bg-gray-300 rounded-[2.5rem]" />

      {/* 顶部红色横条 */}
      <div className="absolute left-[8rem] right-[8rem] top-0 h-[4rem] bg-red-300 rounded-[2.5rem]" />

      {/* 顶部黑色图形1 (用SVG路径表示) */}
      {/* <svg className="absolute left-[18px] top-[11px]" width="15" height="5" viewBox="0 0 15 5" fill="none">
        <path d="M0 5L0 0L3.75 0L3.75 5L0 5ZM11.25 5L11.25 0L15 0L15 5L11.25 5Z" fill="black" />
      </svg> */}

      {/* 顶部黑色图形2 */}
      {/* <svg className="absolute left-[5px] top-[11px]" width="5" height="5" viewBox="0 0 5 5" fill="none">
        <path d="M0 0L5 0L5 5L0 5L0 0Z" fill="black" />
      </svg> */}

      {/* 底部徽章样式的图形 (简化表示) */}
      {/* <svg className="absolute left-[5px] bottom-[5px]" width="13" height="13" viewBox="0 0 13 13" fill="none">
        <path d="M0 6.5C0 2.91015 2.91015 0 6.5 0C10.0899 0 13 2.91015 13 6.5C13 10.0899 10.0899 13 6.5 13C2.91015 13 0 10.0899 0 6.5Z" fill="black" />
      </svg> */}
    </div>
  );
};

const CardGrid = () => {
  return (
    <div className="grid grid-cols-2 grid-rows-2 gap-8 p-4">
      <Card />
    </div>
  );
};

export default CardGrid;