#ifndef I2S_MIC_H
#define I2S_MIC_H

#include <stddef.h>
#include <stdint.h>

void init_i2s_mic(void);
void i2s_read_audio(void *dest, size_t size, size_t *bytes_read);

#endif // I2S_MIC_H
