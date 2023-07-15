SET(SOURCES screens/ui_ScreenSetup.c
    screens/ui_ScreenWifiReset.c
    screens/ui_ScreenListen.c
    screens/ui_ScreenSettings.c
    screens/ui_ScreenReset.c
    ui.c
    ui_helpers.c
    images/ui_eyes_temp.c
    images/ui_img_setup_text_bg_png.c
    images/ui_img_reset_icon_png.c
    images/ui_img_settings_back_png.c
    images/ui_img_reply_chatgpt_bg_png.c
    images/ui_img_sleep_big_z_png.c
    images/ui_img_reply_body_png.c
    images/ui_img_reply_small_shadow_png.c
    images/ui_img_reply_chatgpt_logo_png.c
    images/ui_img_settings_icon_png.c)

add_library(ui ${SOURCES})
