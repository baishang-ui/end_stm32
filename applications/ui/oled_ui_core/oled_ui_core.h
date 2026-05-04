#ifndef __OLED_UI_H
#define __OLED_UI_H
// 检测是否是C++编译器
#ifdef __cplusplus
extern "C" {
#endif
/*如果需要使用OLED_UI库，请不要将此行注释掉 */
#define OLED_UI 

#ifdef OLED_UI


#include "oled_hal_input.h"
#include "oled_graphics.h"
#include "stdint.h"
#include "stdbool.h"

// 显存缓存区
extern uint8_t OLED_DisplayBuf[OLED_HEIGHT/8][OLED_WIDTH];

/************************************************************/
/***********************用户可以配置的宏定义******************/

// /***********关于列表类菜单项开始点相对于菜单区域的位置的宏***********/
// #define LIST_STARTPOINT_X                  (4)           //菜单项开始点相对于菜单区域的X坐标
// #define LIST_STARTPOINT_Y                  (2)           //菜单项开始点相对于菜单区域的Y坐标

/***********关于磁贴类菜单项开始点相对屏幕顶部位置的宏***********/
#define TILES_STARTPOINT_Y          (6)         //磁贴类菜单项开始点相对于屏幕顶部的Y坐标

/***********关于磁贴类菜单项的滚动条开始点相对磁贴底部位置的宏***********/
#define TILES_SCROLLBAR_Y           (5)         //磁贴类菜单项开始点相对于屏幕顶部的Y坐标

/***********关于磁贴类菜单项文字距离屏幕底部的距离的宏***********/
#define TILES_BOTTOM_DISTANCE                       (0)         //磁贴类菜单项文字距离屏幕底部的距离

/************关于菜单项横向滚动速度的宏**********************/
//当菜单项长度超过菜单区域宽度时，本ui框架会自动实现菜单项的横向滚动显示，这是菜单项横向滚动步长（每一帧滚动的像素距离）
#define LINE_SLIP_SPEED         (0.2f)          //菜单项横向滚动速度【可以是小数，但是最好是1的整数倍或者1的偶数分之一倍】
#define GIFICON_SLIP_SPEED          (100)           //Gif的播放速度【每 XX FPS播放一帧Gif】


/************关于菜单项前缀符号的宏**********************/

#define FUNCTION_PREFIX             "~"         //函数前缀符号
#define SUBMENU_PREFIX              ">"         //菜单前缀符号
#define RADIO_PREFIX                "*"         //单选框前缀符号
#define NONE_PREFIX                 "-"         //无操作前缀符号

#define LINEPERFIX_DISTANCE         (2)         //行前缀符号与后方文字的距离

/*******************关于是否等待动画加载完毕再滚动长文本的宏******************/

#define IF_WAIT_ANIMATION_FINISH          (true)

/*******************关于长按按键的宏******************/
#define IF_START_UP_AND_DOWN_LONG_PRESS    (true)   //是否开启上按键与下按键的长按功能
#define PRESS_TIME                      (0.8)    //按下按键视为长按的时间（单位秒）
#define CONTINUE_PRESS_TIME             (2)    //在长按0.8秒之后继续长按的时间（单位秒）
#define PRESS_MOVE_SPEED                (10)      //长按按键之后选项滚动的速度，1为最快，数字越大越慢。
#define CONTINUE_PRESS_MOVE_SPEED       (1)      //长按按键之后继续长按CONTINUE_PRESS_TIME秒后选项滚动的速度，1为最快，数字越大越慢。
/*******************关于窗口文字与数据之间距离的宏******************/
#define WINDOW_DATA_TEXT_DISTANCE           (4)

/**************关于淡出每帧的时间的宏**********/
#define FADEOUT_TIME                    (40)            //菜单项淡出每帧的时间

/************************************************************/







/**********用户无需配置的宏定义【以下宏定义不要改动】********/

/***********有关光标类型的宏***********/
#define REVERSE_RECTANGLE               (0)         //矩形反色
#define REVERSE_ROUNDRECTANGLE      (1)         //圆角矩形反色
#define HOLLOW_RECTANGLE                    (2)         //空心矩形
#define HOLLOW_ROUNDRECTANGLE           (3)         //空心圆角矩形
#define REVERSE_BLOCK                       (4)         //下划线
#define NOT_SHOW                            (5)         //不显示光标

/***********关于UI字体大小的宏***********/
#define OLED_UI_FONT_8           (8)
#define OLED_UI_FONT_12             (12)
#define OLED_UI_FONT_16             (16)
#define OLED_UI_FONT_20             (20)

#define CHINESE                     (0)         //中文
#define ASCII                       (1)         //ASCII
/***********关于显示模式的宏***********/
#define DARKMODE                        true            //深色模式
#define LIGHTMODE                       false           //浅色模式



/***********关于互斥锁或者标志位的宏***********/
#define FLAGEND                   (0)
#define FLAGSTART             (1)
#define ENTER_FLAGSTART               (1)
#define BACK_FLAGSTART            (2)


/************关于菜单结构体当中关于菜单区域的宏***********/
#define FULLSCREEN      1,1,OLED_WIDTH-2,OLED_HEIGHT-2

/*********************关于确认返回的宏********************** */
#define OLED_UI_BACK                (0)
#define OLED_UI_ENTER               (1)

/*********************关于菜单类型的宏********************** */

#define MENU_TYPE_LIST              (0)         //列表类型
#define MENU_TYPE_TILES             (1)         //磁贴类型
#define MENU_TYPE_EMPTY             (2)         //空白类型

/*********************关于动画类型的宏********************** */
#define UNLINEAR                  (0)
#define EASE_CURVE                (1)

/*********************关于窗口类型的宏********************** */
#define WINDOW_RECTANGLE            (0)         //矩形窗口
#define WINDOW_ROUNDRECTANGLE       (1)         //圆角矩形窗口

/*********************关于窗口数据类型的宏* ***************/
#define WINDOW_DATA_STYLE_NONE      (-1)            //空
#define WINDOW_DATA_STYLE_FLOAT     (0)         //浮点型数据
#define WINDOW_DATA_STYLE_INT       (1)         //整型数据



/***************************宏定义****************************/
/************************************************************/

/*表示菜单类型的数据结构*/
typedef int8_t MenuStyle;


/*表示菜单ID的数据结构*/
typedef int16_t MenuID;

/*表示菜单ID的数据结构*/
typedef float MenuMovingSpeed;

/*表示菜单ID的数据结构*/
typedef int8_t OLED_Font;

/*表示互斥锁或标志位的数据结构，只有两种状态FLAGSTART与FLAGEND*/
typedef int8_t MutexFlag;

/*菜单id结构体，存储着经过限制的菜单id和不经过限制的菜单id*/
typedef struct MenuID_Type{
    MenuID Safe;
    MenuID Unsafe;
}MenuID_Type;

/*OLED_UI当中有点坐标的结构体*/
typedef struct OLED_Point{
    float X;
    float Y;
} OLED_Point;

/*OLED_UI当中有关位置与大小的结构体*/
typedef struct  OLED_Area{
    float X;
    float Y;
    float Width;
    float Height;
} OLED_Area;

/*OLED_UI当中用于存放按键阵列数据的结构体*/
typedef struct OLED_Key{ 
    uint8_t Enter;  //确认键
    uint8_t Back;   //返回键
    uint8_t Up;     //上
    uint8_t Down;   //下
} OLED_Key;

/*OLED_UI当中存放光标信息的结构体【用于制造光标移动效果】 */
typedef struct oled_ui_anim_area{
    OLED_Area CurrentArea;  //当前光标区域
    OLED_Area TargetArea;       //目标光标区域
    OLED_Area Error;            //误差值
    OLED_Area LastError;        //上一次的误差值
    OLED_Area Integral;     //积分值
    OLED_Area Derivative;       //微分值

} oled_ui_anim_area_t;

typedef struct oled_ui_anim_point{
    OLED_Point CurrentPoint;    //当前点坐标
    OLED_Point TargetPoint;     //目标点坐标
    OLED_Point Error;           //误差值
    OLED_Point LastError;       //上一次的误差值
    OLED_Point Integral;        //积分值
    OLED_Point Derivative;      //微分值

} oled_ui_anim_point_t;

/**
 * @brief 此结构体用于计算动画，兼容非线性与 ease 曲线的状态缓存。
 *  @param CurrentDistance 当前值
 *  @param TargetDistance 目标值
 *  @param Error 兼容状态缓存
 *  @param LastError 兼容状态缓存
 *  @param Integral 兼容状态缓存
 *  @param Derivative 兼容状态缓存
 *  */ 
typedef struct oled_ui_anim_distance{
    float CurrentDistance;      //当前值
    float TargetDistance;       //目标值
    float Error;                    //误差值
    float LastError;                //上一次的误差值
    float Integral;             //积分值
    float Derivative;               //微分值

} oled_ui_anim_distance_t;

/*OLED_UI当中存放计数器信息的结构体*/
typedef struct OLED_UI_Counter{
    int16_t count;
    int16_t value;
    int16_t step;
}OLED_UI_Counter;

/*OLED_UI当中用于实现窗口停留的结构体*/
typedef struct OLED_UI_WindowSustainCounter{
    int16_t count;      //计数器
    bool SustainFlag;   //标志位，用于表示是否处于停留状态
}OLED_UI_WindowSustainCounter;

/*OLED_UI当中菜单窗口的结构体*/
typedef struct MenuWindow{
    int16_t General_Width;                                  //[通用属性]窗口宽度
    int16_t General_Height;                                 //[通用属性]窗口高度
    float General_ContinueTime;                             //[通用属性]窗口停留时间
    uint8_t General_WindowType;                             //[通用属性]窗口类型

    char* Text_String;                                      //[文本属性]窗口显示的文字
    OLED_Font Text_FontSize;                                //[文本属性]窗口字体大小
    int16_t Text_FontSideDistance;                      //[文本属性]字体距离左侧的距离
    int16_t Text_FontTopDistance;                       //[文本属性]字体距离顶部的距离




    float* Prob_Data_Float;                                     //[进度条属性]窗口数据指针(浮点类型)【二选一】
    int16_t* Prob_Data_Int;                                     //[进度条属性]窗口数据指针(int16_t整型类型)【二选一】
    float Prob_DataStep;                                    //[进度条属性]窗口数据步长
    float Prob_MinData;                                 //[进度条属性]窗口数据的最小值
    float Prob_MaxData;                                 //[进度条属性]窗口数据的最大值
    int16_t Prob_BottomDistance;                            //[进度条属性]进度条底部距离窗口底部的距离
    int16_t Prob_SideDistance;                              //[进度条属性]进度条边缘距离窗口边缘的距离
    int16_t Prob_LineHeight;                                //[进度条属性]进度条线条高度
    bool Prob_ShowBar;                                      //[进度条属性]是否显示进度条

    float _LineSlip;                                        //[私有属性]用于滚动显示长文本
}MenuWindow;




/*OLED_UI当中菜单项的结构体，用于存放菜单项的相关信息，
包括名称，字体大小，回调函数，子菜单指针，父菜单指针，开始点，选中区域，是否活跃状态，是否是最后一个菜单项*/
typedef struct MenuPage {
    MenuStyle General_MenuType;
    MenuMovingSpeed General_MovingSpeed;
    uint8_t General_CursorStyle;
    uint8_t General_MoveStyle;
    OLED_Font General_FontSize;
    struct MenuPage* General_ParentMenuPage;
    struct MenuItem* General_MenuItems;
    int16_t General_LineSpace;
    void (*General_ShowAuxiliaryFunction)(void);

    OLED_Area List_MenuArea;
    bool List_IfDrawFrame;
    bool List_IfDrawLinePerfix;
    int16_t List_StartPointX;
    int16_t List_StartPointY;

    int16_t Tiles_ScreenWidth;
    int16_t Tiles_ScreenHeight;
    int16_t Tiles_TileWidth;
    int16_t Tiles_TileHeight;

    MenuID _ActiveMenuID;
    MenuID _Slot;
    OLED_Point _StartPoint;
} MenuPage;

typedef struct MenuItem {
    char* General_item_text;
    void (*General_callback)(void);
    MenuPage* General_SubMenuPage;

    bool* List_BoolRadioBox;
        int16_t* List_IntBox;
        float* List_FloatBox;
    const uint8_t* Tiles_Icon;
        const uint8_t (*Tiles_GifIcon)[128];

    float _LineSlip;
        uint8_t Gif_index;
} MenuItem;

/******************************************************************************** */
/******************************************************************************** */
/**
 * @brief 更新 UI 帧率统计数据。
 * @param 无。
 * @return 无。
 */
void oled_ui_update_fps(void);

/**
 * @brief 在屏幕上绘制当前帧率信息。
 * @param 无。
 * @return 无。
 */
void oled_ui_render_fps(void);

/**
 * @brief 根据字号和字符类型获取实际绘制字号。
 * @param fontsize 目标字号。
 * @param style 字符类型，CHINESE 或 ASCII。
 * @return 实际使用的 OLED 字号。
 */
OLED_Font oled_ui_get_font(OLED_Font fontsize,bool style);

/**
 * @brief 按指定样式反显或绘制光标区域。
 * @param X 区域左上角 X 坐标。
 * @param Y 区域左上角 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 * @param Style 光标样式。
 * @return 无。
 */
void oled_ui_reverse_coordinate(int16_t X, int16_t Y, int16_t Width, int16_t Height,uint8_t Style);

/**
 * @brief 对指定区域应用一层淡出遮罩。
 * @param x0 区域左上角 X 坐标。
 * @param y0 区域左上角 Y 坐标。
 * @param width 区域宽度。
 * @param height 区域高度。
 * @param fadeLevel 淡出等级。
 * @return 无。
 */
void oled_ui_apply_fade_mask(int16_t x0, int16_t y0, int16_t width, int16_t height, int8_t fadeLevel);

/**
 * @brief 对当前区域执行淡出处理。
 * @param x 区域左上角 X 坐标。
 * @param y 区域左上角 Y 坐标。
 * @param width 区域宽度。
 * @param height 区域高度。
 * @return 无。
 */
void oled_ui_fade_current_area(int16_t x, int16_t y, int16_t width, int16_t height);

/**
 * @brief 对全部需要淡出的区域执行淡出处理。
 * @param 无。
 * @return 无。
 */
void oled_ui_fade_all_areas(void);

/**
 * @brief 统计菜单项数组中的有效菜单项数量。
 * @param items 菜单项数组指针。
 * @return 有效菜单项数量。
 */
MenuID oled_ui_get_menu_item_count(MenuItem * items);

/**
 * @brief 使用当前动画算法推进一个标量值。
 * @param CurrentNum 当前值指针。
 * @param TargetNum 目标值指针。
 * @param ErrorNum 当前误差缓存指针。
 * @param LastErrorNum 上一次误差缓存指针。
 * @param IntegralNum 积分缓存指针。
 * @param DerivativeNum 微分缓存指针。
 * @return 无。
 */
void oled_ui_animate_scalar(float *CurrentNum, float *TargetNum, float *ErrorNum, float *LastErrorNum, float * IntegralNum, float *DerivativeNum);

/**
 * @brief 推进距离动画状态。
 * @param distance 距离动画状态对象。
 * @return 无。
 */
void oled_ui_animate_distance(oled_ui_anim_distance_t *distance);

/**
 * @brief 推进点坐标动画状态。
 * @param point 点动画状态对象。
 * @return 无。
 */
void oled_ui_animate_point(oled_ui_anim_point_t *point);

/**
 * @brief 推进矩形区域动画状态。
 * @param area 区域动画状态对象。
 * @return 无。
 */
void oled_ui_animate_area(oled_ui_anim_area_t *area);

/**
 * @brief 初始化当前页面的运行状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_init_current_page(void);

/**
 * @brief 恢复当前页面的父页面指针和层级状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_restore_parent_page(void);

/**
 * @brief 初始化 OLED UI 框架并进入指定首页。
 * @param Page 首页页面对象。
 * @return 无。
 */
void oled_ui_init(MenuPage* Page);

/**
 * @brief 检查确认键是否处于可响应状态。
 * @param 无。
 * @return 空闲返回 true，否则返回 false。
 */
bool oled_ui_is_enter_idle(void);

/**
 * @brief 检查淡出动画是否处于空闲状态。
 * @param 无。
 * @return 空闲返回 true，否则返回 false。
 */
bool oled_ui_is_fade_idle(void);

/**
 * @brief 请求刷新一块指定区域。
 * @param x 区域左上角 X 坐标。
 * @param y 区域左上角 Y 坐标。
 * @param width 区域宽度。
 * @param height 区域高度。
 * @return 无。
 */
void oled_ui_request_refresh_area(int16_t x, int16_t y, int16_t width, int16_t height);

/**
 * @brief 请求刷新当前页面的主显示区域。
 * @param 无。
 * @return 无。
 */
void oled_ui_request_current_page_refresh(void);

/**
 * @brief 强制下一帧执行整屏刷新。
 * @param 无。
 * @return 无。
 */
void oled_ui_request_full_refresh(void);

/**
 * @brief 计算原始字符串的显示宽度。
 * @param ChineseFont 中文字号。
 * @param ASCIIFont ASCII 字号。
 * @param String UTF-8 字符串。
 * @return 字符串像素宽度。
 */
int16_t oled_ui_calc_raw_string_width(int16_t ChineseFont, int16_t ASCIIFont, const char *String);

/**
 * @brief 按 printf 格式计算字符串显示宽度。
 * @param ChineseFont 中文字号。
 * @param ASCIIFont ASCII 字号。
 * @param format printf 风格格式化字符串。
 * @return 字符串像素宽度。
 */
int16_t oled_ui_calc_string_width(int16_t ChineseFont, int16_t ASCIIFont, const char *format, ...);

/**
 * @brief 判断窗口绑定数据的数据类型。
 * @param int16_tdata 整型数据指针。
 * @param float_tdata 浮点数据指针。
 * @return WINDOW_DATA_STYLE_* 类型。
 */
int8_t oled_ui_get_window_data_style(int16_t *int16_tdata,float *float_tdata);

/**
 * @brief 绘制当前弹窗。
 * @param 无。
 * @return 无。
 */
void oled_ui_draw_window(void);

/**
 * @brief 将列表或磁贴选择向上移动。
 * @param 无。
 * @return 无。
 */
void oled_ui_move_items_up(void);

/**
 * @brief 将列表或磁贴选择向下移动。
 * @param 无。
 * @return 无。
 */
void oled_ui_move_items_down(void);

/**
 * @brief 将磁贴选择向左移动。
 * @param 无。
 * @return 无。
 */
void oled_ui_move_items_left(void);

/**
 * @brief 将磁贴选择向右移动。
 * @param 无。
 * @return 无。
 */
void oled_ui_move_items_right(void);

/**
 * @brief 获取当前页面可选槽位数量。
 * @param 无。
 * @return 当前页面最大槽位数量。
 */
int16_t oled_ui_get_current_page_max_slot_count(void);

/**
 * @brief 根据当前选中项设置目标光标区域。
 * @param 无。
 * @return 无。
 */
void oled_ui_set_target_cursor(void);

/**
 * @brief 设置目标菜单边框动画状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_set_target_menu_frame(void);

/**
 * @brief 重置长文本横向滚动状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_reset_line_slip(void);

/**
 * @brief 设置滚动条目标高度。
 * @param 无。
 * @return 无。
 */
void oled_ui_set_target_scrollbar_height(void);

/**
 * @brief 设置弹窗进度条目标宽度。
 * @param 无。
 * @return 无。
 */
void oled_ui_set_target_progress_width(void);

/**
 * @brief 绘制列表项前缀符号。
 * @param page 当前页面对象。
 * @param i 菜单项索引。
 * @param CursorPoint 当前菜单项起始点。
 * @param ChineseFont 中文字号。
 * @param ASCIIFont ASCII 字号。
 * @return 无。
 */
void oled_ui_draw_line_prefix(MenuPage* page,MenuID i,OLED_Point* CursorPoint,OLED_Font ChineseFont,OLED_Font ASCIIFont);

/**
 * @brief 绘制当前页面所有菜单元素。
 * @param 无。
 * @return 无。
 */
void oled_ui_draw_menu_elements(void);

/**
 * @brief 轮询按键和旋钮输入并返回安全/非安全菜单索引。
 * @param 无。
 * @return 菜单索引状态。
 */
MenuID_Type oled_ui_poll_key_and_encoder(void);

/**
 * @brief 锁定确认键，避免动画或回调期间重复触发。
 * @param 无。
 * @return 无。
 */
void oled_ui_lock_enter(void);

/**
 * @brief 解锁确认键，使其重新响应输入。
 * @param 无。
 * @return 无。
 */
void oled_ui_unlock_enter(void);

/**
 * @brief 设置淡出动画标志。
 * @param action FLAGSTART 或 FLAGEND。
 * @return 无。
 */
void oled_ui_set_fade_flag(MutexFlag action);

/**
 * @brief 重置淡出动画标志。
 * @param 无。
 * @return 无。
 */
void oled_ui_reset_fade_flag(void);

/**
 * @brief 进入当前选中的菜单项或执行其回调。
 * @param 无。
 * @return 无。
 */
void oled_ui_enter_menu_item(void);

/**
 * @brief 从当前菜单项返回上一层页面。
 * @param 无。
 * @return 无。
 */
void oled_ui_back_menu_item(void);

/**
 * @brief 执行统一返回动作。
 * @param 无。
 * @return 无。
 */
void oled_ui_back(void);

/**
 * @brief 运行当前选中菜单项绑定的回调函数。
 * @param 无。
 * @return 无。
 */
void oled_ui_run_current_callback(void);

/**
 * @brief 重置光标动画和选中状态。
 * @param 无。
 * @return 无。
 */
void oled_ui_reset_cursor(void);

/**
 * @brief 创建并显示一个菜单弹窗。
 * @param window 弹窗配置对象。
 * @return 无。
 */
void oled_ui_create_window(MenuWindow* window);

/**
 * @brief 执行菜单淡出动画。
 * @param 无。
 * @return 无。
 */
void oled_ui_run_fade_out(void);

/**
 * @brief 推进菜单元素动画并更新目标位置。
 * @param 无。
 * @return 无。
 */
void oled_ui_move_menu_elements(void);

/**
 * @brief 执行一帧 OLED UI 主循环。
 * @param 无。
 * @return 无。
 */
void oled_ui_main_loop(void);

/**
 * @brief 处理 UI 输入中断或周期性输入采样。
 * @param 无。
 * @return 无。
 */
void oled_ui_interrupt_handler(void);     
#endif  /* OLED_UI */

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  /* __OLED_UI_H */



