# DM-MC02 Soft3D

## 项目介绍

在 DM-MC02 的 STM32H723 上用 C 实现软件 3D 渲染，将带纹理的模型画到板载 280 × 240 LCD。无需 GPU，也不需要额外硬件。

项目包含两个模式：倾斜开发板控制小球的重力迷宫，以及展示迷宫、立方体、圆环和轨道场景的 Engine Lab。Lab 可切换渲染索引并查看帧时间，用于比较板端渲染开销。

![Engine Lab 场景预览](Docs/engine-lab-preview.png)

上图由同一套 C 渲染器在电脑上生成，图中初始计时值不代表实板性能。

## 使用说明

### 烧录与编译

可直接通过 SWD 烧录 [Engine Lab V4 固件](Firmware/DM_MC02_Soft3D_EngineLab_v4.hex)。

需要修改代码时，用 Keil MDK（ARM Compiler 5）打开 `MDK-ARM/DM_MC02_Soft3D.uvprojx`，选择同名目标并编译。也可在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File Tools/build.ps1 -Rebuild
```

脚本可用 `-Uv4Path` 指定 Keil 路径。新编译的固件位于 `MDK-ARM/DM_MC02_Soft3D/DM_MC02_Soft3D.hex`。

### 重力迷宫

上电后将开发板静置约 5 秒，等待姿态初始化。屏幕显示 `ENGINE LAB V4` 状态页后，短按中键进入迷宫，倾斜开发板让小球滚到终点。

| 按键 | 操作 |
| --- | --- |
| 中键短按 | 暂停 / 继续 |
| 中键长按约 0.8 秒 | 重开当前关卡，并重新设定中立姿态 |
| 上键 | 将当前姿态设为中立，停止小球 |
| 右键 | 下一关 |
| 下键短按 | 进入 Engine Lab |
| 下键长按约 0.8 秒 | 返回状态页；短按中键恢复 |

### Engine Lab

| 按键 | 操作 |
| --- | --- |
| 右键 | 切换迷宫、立方体、圆环、轨道场景 |
| 左键 | 切换 `INDEX ON / OFF` |
| 中键短按 | 暂停 / 继续动画 |
| 中键长按约 0.8 秒 | 复位姿态和统计 |
| 下键短按 | 返回迷宫 |
| 下键长按约 0.8 秒 | 返回状态页 |

屏幕显示几何处理、光栅化、传输等待和整帧耗时；P95 在采集满 64 帧后显示。USB 虚拟串口每秒输出一次诊断数据，不连接电脑也能运行。

引擎设计见 [ENGINE.md](Docs/ENGINE.md)，已完成的验证和待实测项见 [VALIDATION.md](Docs/VALIDATION.md)。
