// File: d:\dev_device\bamboo_slicer\resources\web\device_page\src\HomePage.tsx

import React from 'react';

const HomePage: React.FC = () => {
    const handleClick = () => {
        alert('按钮被点击了！');
    };

    return (
        <div>
            <h1 className='text-h1'>欢迎来到主页</h1>
            <button onClick={handleClick}>点击我</button>
            <div>
                <button style={
                    { backgroundColor: "#1abc9c" }
                }>

                </button>
            </div>
        </div>

    );
};

export default HomePage;
