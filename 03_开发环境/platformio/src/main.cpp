/*
 * StackChan PlatformIO 入口 (Arduino 框架)
 * 硬件: M5Stack StackChan (SKU K151, CoreS3 / ESP32-S3)
 *
 * 功能: 屏幕显示 + 顶部触摸检测 + 舵机动作演示
 *
 * 依赖见 platformio.ini:
 *   - M5Unified, M5GFX
 *   - M5StackChan (需本地引入, 见 platformio.ini 注释)
 *
 * !!! 安全: Y 轴舵机限位 5~85 度 (API 50~850), 切勿手动强转舵机 !!!
 */

#include <M5StackChan.h>

int state = 1;
const int MAX_STATE = 5;

void setup() {
    M5StackChan.begin();
    M5StackChan.Motion.goHome();

    M5StackChan.Display().setTextSize(2);
    M5StackChan.Display().setTextScroll(true);
    M5StackChan.Display().setTextColor(TFT_ORANGE);
    M5StackChan.Display().printf("> StackChan PlatformIO\n");
    M5StackChan.Display().printf("> Touch top to start\n");
    M5StackChan.Display().setTextColor(TFT_GREEN);
}

void loop() {
    M5StackChan.update();

    if (M5StackChan.TouchSensor.wasPressed()) {
        // 角度单位 10 = 1 度; 速度 0~1000
        switch (state) {
            case 1:
                M5StackChan.Motion.move(0, 450);     // 居中平视
                M5StackChan.Display().printf("> Center\n");
                break;
            case 2:
                M5StackChan.Motion.moveX(900, 500);  // 向左
                M5StackChan.Display().printf("> Left\n");
                break;
            case 3:
                M5StackChan.Motion.moveX(-900, 500); // 向右
                M5StackChan.Display().printf("> Right\n");
                break;
            case 4:
                M5StackChan.Motion.moveY(850, 300);  // 抬头(安全上限)
                M5StackChan.Display().printf("> Up\n");
                break;
            default:
                M5StackChan.Motion.goHome();
                M5StackChan.Display().printf("> Home\n");
                break;
        }
        state++;
        if (state > MAX_STATE) state = 1;
    }
    delay(10);
}
