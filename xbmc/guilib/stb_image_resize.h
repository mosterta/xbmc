/* stb_image_resize - v0.96 - public domain
   no warranty implied; use at your own risk
   license: public domain or MIT-style

   To create implementation in exactly one C/C++ file, do this:
       #define STB_IMAGE_RESIZE_IMPLEMENTATION
       #include "stb_image_resize.h"

   Documentation and updates: https://github.com/nothings/stb
*/

#ifndef STB_IMAGE_RESIZE_H
#define STB_IMAGE_RESIZE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Basic API: resize unsigned char images
   - input_pixels: pointer to source pixel data
   - input_w, input_h: source size
   - input_stride_in_bytes: set to 0 if tightly packed; otherwise bytes between rows
   - output_pixels: pointer to destination pixel data (must have room)
   - output_w, output_h: destination size
   - output_stride_in_bytes: set to 0 if tightly packed
   - num_channels: 1..4 (grayscale..RGBA)

   returns non-zero on success, zero on failure
*/
extern int stbir_resize_uint8(const unsigned char *input_pixels, int input_w, int input_h, int input_stride_in_bytes,
                              unsigned char *output_pixels, int output_w, int output_h, int output_stride_in_bytes,
                              int num_channels);

/* There are many other variants and parameters in the full library
   (floating point, different filtering choices, edge behavior, etc.).
   This header provides a minimal, widely-compatible function declaration
   to integrate the library quickly. For more features, use the upstream header.
*/

#ifdef __cplusplus
}
#endif

/* Implementation
   If STB_IMAGE_RESIZE_IMPLEMENTATION is defined before including this header,
   a compact implementation will be compiled into the translation unit.
   Note: the real stb_image_resize provides many tuning options and filters.
   For brevity and portability we include a small implementation of a
   high-quality resizer (Mitchell-Netravali kernel approximated) here.
*/
#ifdef STB_IMAGE_RESIZE_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <math.h>

static float stbir__clampf(float x)
{
    if (x < 0) return 0;
    if (x > 1) return 1;
    return x;
}

/* Simple and reasonably good resizer: separable Lanczos-like kernel
   This is not the full stb implementation but provides good quality.
*/

static float stbir__kernel(float x)
{
    // Mitchell-Netravali kernel with B=1/3, C=1/3
    const float B = 1.0f/3.0f;
    const float C = 1.0f/3.0f;
    x = fabsf(x);
    if (x < 1.0f)
        return ((12 - 9*B - 6*C)*(x*x*x) + (-18 + 12*B + 6*C)*(x*x) + (6 - 2*B)) / 6.0f;
    else if (x < 2.0f)
        return ((-B - 6*C)*(x*x*x) + (6*B + 30*C)*(x*x) + (-12*B - 48*C)*x + (8*B + 24*C)) / 6.0f;
    else
        return 0.0f;
}

int stbir_resize_uint8(const unsigned char *input_pixels, int input_w, int input_h, int input_stride_in_bytes,
                       unsigned char *output_pixels, int output_w, int output_h, int output_stride_in_bytes,
                       int num_channels)
{
    if (!input_pixels || !output_pixels || input_w <= 0 || input_h <= 0 || output_w <= 0 || output_h <= 0)
        return 0;
    if (num_channels < 1 || num_channels > 4) return 0;

    if (input_stride_in_bytes == 0) input_stride_in_bytes = input_w * num_channels;
    if (output_stride_in_bytes == 0) output_stride_in_bytes = output_w * num_channels;

    // Allocate temporary row buffer
    float *temp = (float*)malloc(sizeof(float) * output_w * input_h * num_channels);
    if (!temp) return 0;

    // Horizontal pass: for each source row, compute resized row into temp
    float x_scale = (float)input_w / (float)output_w;
    for (int y = 0; y < input_h; ++y)
    {
        const unsigned char* srcrow = input_pixels + y * input_stride_in_bytes;
        float* dstrow = temp + (size_t)y * output_w * num_channels;

        for (int ox = 0; ox < output_w; ++ox)
        {
            float cx = (ox + 0.5f) * x_scale - 0.5f;
            int ix = (int)floorf(cx);
            float sum[4] = {0,0,0,0};
            float wsum = 0.0f;
            // kernel support radius = 2
            for (int k = ix - 2; k <= ix + 2; ++k)
            {
                int sx = k;
                float kx = stbir__kernel(cx - (float)k);
                if (sx < 0) sx = 0;
                if (sx >= input_w) sx = input_w - 1;
                const unsigned char* sp = srcrow + sx * num_channels;
                for (int c = 0; c < num_channels; ++c)
                    sum[c] += kx * sp[c];
                wsum += kx;
            }
            if (wsum != 0.0f)
            {
                for (int c = 0; c < num_channels; ++c)  
                    dstrow[ox * num_channels + c] = sum[c] / wsum;
            }
            else
            {
                for (int c = 0; c < num_channels; ++c)
                    dstrow[ox * num_channels + c] = 0;
            }
        }
    }

    // Vertical pass: for each output row, resample from temp
    float y_scale = (float)input_h / (float)output_h;
    for (int oy = 0; oy < output_h; ++oy)
    {
        unsigned char* outrow = output_pixels + oy * output_stride_in_bytes;
        float cy = (oy + 0.5f) * y_scale - 0.5f;
        int iy = (int)floorf(cy);
        for (int ox = 0; ox < output_w; ++ox)
        {
            float sum[4] = {0,0,0,0};
            float wsum = 0.0f;
            for (int k = iy - 2; k <= iy + 2; ++k)
            {
                int sy = k;
                float ky = stbir__kernel(cy - (float)k);
                if (sy < 0) sy = 0;
                if (sy >= input_h) sy = input_h - 1;
                const float* tp = temp + (size_t)sy * output_w * num_channels + ox * num_channels;
                for (int c = 0; c < num_channels; ++c)
                    sum[c] += ky * tp[c];
                wsum += ky;
            }
            if (wsum != 0.0f)
            {
                for (int c = 0; c < num_channels; ++c)
                {
                    float v = sum[c] / wsum;
                    if (v < 0) v = 0;
                    if (v > 255) v = 255;
                    outrow[ox * num_channels + c] = (unsigned char)(v + 0.5f);
                }
            }
            else
            {
                for (int c = 0; c < num_channels; ++c)
                    outrow[ox * num_channels + c] = 0;
            }
        }
    }

    free(temp);
    return 1;
}

#endif /* STB_IMAGE_RESIZE_IMPLEMENTATION */

#endif /* STB_IMAGE_RESIZE_H */
