/**
  ******************************************************************************
  * @file    web_server.h
  * @brief   Minimal HTTP/1.1 server (BSD sockets) for the LED/buzzer demo.
  ******************************************************************************
  */
#ifndef APP_WEB_SERVER_H_
#define APP_WEB_SERVER_H_

/** Spawn the HTTP server thread. Returns 0 when the thread was created. */
int web_server_start(void);

#endif /* APP_WEB_SERVER_H_ */
