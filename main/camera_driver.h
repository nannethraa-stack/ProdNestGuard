#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t init_camera(void);
camera_fb_t* capture_camera_frame(void);
void release_camera_frame(camera_fb_t *fb);

#endif // CAMERA_DRIVER_H
