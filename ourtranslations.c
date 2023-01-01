/*
* Our Paint: A light weight GPU powered painting program.
* Copyright (C) 2022 Wu Yiming
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ourpaint.h"

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

static const char *entries[]={
"Website","网站",
"Save on exit:","退出时保存：",
"Cover artist:","封面作者：",
"Donate","打钱",
"Donate (China)","打钱（支付宝）",
"Development logs","开发日志",
"Our Paint is a free application.","好得涂是自由程序。",
"User Manual","用户手册",
"Support the development:","支持开发：",
"Select the exporting behavior:","选择导出行为：",
"Image ICC","图像内置 ICC",
"Output:","输出：",
"16 Bits","16位",
"Input image does not have a built-in color profile.","输入图像未包含内置色彩配置描述。",
"16 bit images would be exported in the same linear color space as the canvas","16位深图像会按照和画布相同的线性色彩空间导出",
"Input image is tagged as sRGB.","输入图像已标记为sRGB",
"Force Linear sRGB","强制线性sRGB",
"Force sRGB","强制sRGB",
"Flat","平直",
"Force Linear Clay","强制线性Clay",
"Follow Canvas","跟随画布",
"Canvas Current:","画布当前：",
"Select the importing behavior:","选择导入行为：",
"Input image is not tagged as sRGB.","输入图片并未被标记为sRGB",
"Force Clay","强制Clay",
"Canvas:","画布：",
"Input:","输入：",
"Input image has built-in color profile:","输入图像带有内置色彩配置描述：",
"Brush Nodes","笔刷节点",
"Our Paint","好得涂",
"Brushes","笔刷",
"Layers","图层",
"Canvas","画布",
"Unlocked","已解锁",
"Position:","位置：",
"Size:","尺寸：",
"Brush tool not selected","未选择笔刷工具",
"Lock","锁定",
"Border Alpha","边框透明度",
"No","无",
"Our Paint is made by Wu Yiming.","好得涂 由吴奕茗制作。",
"A simple yet flexible node-based GPU painting program.","一个简单灵活的节点控制GPU绘画程序。",
"Our Paint blog","好得涂博客",
"Dev log","开发日志",
"Single canvas implementation.","单画布实现。",
"8 Bits","8位",
"Color Profile:","色彩配置：",
"Import Layer","导入图层",
"Merge","合并",
"Cropping","裁剪",
"Smoothness","平滑度",
"Lock Radius","锁定半径",
"Smudge","涂抹",
"Clean","干净",
"Layer","图层",
"Image","图像",
"Paint","涂画",
"Multiply","相乘",
"Visible","可见",
"Linear sRGB","线性 sRGB",
"Generic:","通用：",
"Transparency","透明度",
"New Brush","新笔刷",
"Canvas Scale","画布缩放",
"Others","其他",
"Bit Depth:","位深度：",
"Our Paint v0.1","好得涂 v0.1",
"New Layer","新图层",
"Color Space:","色彩空间：",
"Our Paint","好得涂",
"Dabs Per Size","每半径的笔触点数",
"Paintable","可绘图",
"Use Nodes","使用节点",
"Paint Undo Limit","绘图撤销限制",
"Show","显示",
"Background:","背景：",
"Angle","角度",
"Brush Circle","笔刷圆圈",
"Erasing","擦除",
"R,G,B","红,绿,蓝",
"Assign all \"Our Tools\" into:","将所有“工具”指定到： ",
"Min,Max","最小,最大",
"Developer:","开发者：",
"Smudge Resample Length","涂抹重采样长度",
"Mode:","模式：",
"Display:","显示：",
"Hardness","硬度",
"Export Layer","导出图层",
" Steps","步",
"Modified","已更改",
"Export Image","导出图像",
"Slender","压扁",
"Show debug tiles","显示调试图像块",
"Exporting Defaults:","导出时的默认值：",
"Default as eraser","默认为橡皮擦",
"Name of the brush","笔刷名字",
"Combine","合并",
"Locked","已锁定",
"Brush Device","笔刷设备",
"Brush Outputs","笔刷输出",
"About","关于",
"Linear Clay","线性 Clay",
"Brush Settings","笔刷设置",
0,0};

void ourMakeTranslations(){
    transSetLanguage("zh-CN");
    for(int i=0;;i++){if(!entries[i*2])break;
        transNewEntry(entries[i*2],entries[i*2+1]);
    }
}
