#ifndef AI_INFERENCE_H
#define AI_INFERENCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_camera.h"

void init_edge_ai_models(void);
bool run_audio_inference(const int32_t *audio_buffer, size_t len);
bool run_vision_inference(camera_fb_t *fb);

#endif // AI_INFERENCE_H
