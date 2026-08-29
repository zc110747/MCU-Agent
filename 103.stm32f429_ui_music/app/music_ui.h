/**
  ******************************************************************************
  * @file    app/music_ui.h
  * @brief   Screen 2: the music player UI.
  ******************************************************************************
  */
#ifndef MUSIC_UI_H
#define MUSIC_UI_H

/**
  * @brief  Build the music player screen and start its refresh timer.
  *         The track list must already be populated (player_scan()).
  */
void music_ui_create(void);

#endif /* MUSIC_UI_H */
