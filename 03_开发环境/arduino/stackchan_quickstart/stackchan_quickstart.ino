/*
 * StackChan 快速入门示例(Arduino IDE)
 * 硬件: M5Stack StackChan (SKU K151, CoreS3 / ESP32-S3)
 *
 * 功能:
 *   - 顶部触摸传感器检测(单击 / 前滑 / 后滑 / 按下)
 *   - 舵机运动演示(X 轴水平 / Y 轴垂直 / X 轴 360° 连续旋转)
 *   - 屏幕日志输出
 *
 * 依赖库(通过 Arduino Library Manager 安装):
 *   - M5StackChan  >= 1.0.0
 *   - M5Unified    >= 0.2.11
 *   - M5GFX        >= 0.2.18
 *
 * 板选择: 工具 -> 开发板 -> M5Stack -> M5CoreS3
 * 板管理 URL: https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
 *
 * 烧录前: 长按侧边 RST 键约 3 秒,绿灯亮后松开,进入下载模式
 *
 * !!! 安全警告 !!!
 *   Y 轴(垂直)舵机安全区间 5~85 度(API 写作 50~850),超限可能永久损坏。
 *   X 轴(水平)无角度限制,支持 360° 连续旋转。
 *   切勿用手强行转动舵机。
 */

#include <M5StackChan.h>

int state = 1;
const int MAX_STATE = 8;

void setup() {
    M5StackChan.begin();

    // 舵机归位
    M5StackChan.Motion.goHome();

    // 屏幕初始化
    M5StackChan.Display().setTextSize(2);
    M5StackChan.Display().setTextScroll(true);
    M5StackChan.Display().setTextColor(TFT_ORANGE);
    M5StackChan.Display().printf("> StackChan Quickstart\n");
    M5StackChan.Display().printf("> Touch the top to start\n");
    M5StackChan.Display().setTextColor(TFT_GREEN);
}

void loop() {
    M5StackChan.update();

    // 顶部触摸:每次按下切换一个动作状态
    if (M5StackChan.TouchSensor.wasPressed()) {
        // 角度单位: 10 = 1 度; 速度范围 0~1000
        // X 范围 -1280~1280 (-128~128 度)
        // Y 范围    0~900 (0~90 度, 安全区间 50~850)
        switch (state) {
            case 1:
                M5StackChan.Motion.move(0, 450);     // X=0 度, Y=45 度(居中平视)
                M5StackChan.Display().printf("> Turn Y to 45\n");
                break;
            case 2:
                M5StackChan.Motion.moveX(900, 500);  // X=90 度 向左
                M5StackChan.Display().printf("> Turn Left\n");
                break;
            case 3:
                M5StackChan.Motion.moveX(-900, 500); // X=-90 度 向右
                M5StackChan.Display().printf("> Turn Right\n");
                break;
            case 4:
                M5StackChan.Motion.moveY(850, 300);  // Y=85 度 抬头(安全上限)
                M5StackChan.Display().printf("> Look Up\n");
                break;
            case 5:
                M5StackChan.Motion.moveY(50, 300);   // Y=5 度 低头(安全下限)
                M5StackChan.Display().printf("> Look Down\n");
                break;
            case 6:
                // 仅 X 轴支持 360 度连续旋转: 负=顺时针, 正=逆时针
                M5StackChan.Motion.rotateX(-800);    // 顺时针
                M5StackChan.Display().printf("> Rotate clockwise\n");
                delay(2000);
                M5StackChan.Motion.stop();
                break;
            case 7:
                M5StackChan.Motion.rotateX(800);     // 逆时针
                M5StackChan.Display().printf("> Rotate counter-clockwise\n");
                delay(2000);
                M5StackChan.Motion.stop();
                break;
            default:
                M5StackChan.Motion.goHome();
                M5StackChan.Display().printf("> Go home\n");
                break;
        }
        state++;
        if (state > MAX_STATE) state = 1;
    }
    delay(10);
}
