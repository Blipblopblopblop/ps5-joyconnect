#pragma once
#include <stdint.h>

/* libSceAvcap2 audio capture wrapper — same approach as ghost-toothAPI */
int  avcap_open(int sample_rate, int channels);
int  avcap_read(int16_t *buf, int frames);  /* returns frames read, or <0 on error */
void avcap_close(void);
