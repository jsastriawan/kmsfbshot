/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Read back all the KMS framebuffers attached to the CRTC and record as JPEG.
 * Modified 2018: remove cairo, use JPEG compression copying the code from 
 * cairo_jpg module implementation by Bernhard R. Fischer
 * 
 * https://github.com/rahra/cairo_jpg/blob/master/src/cairo_jpg.c
 * 
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <jpeglib.h>

static void pix_conv(unsigned char *dst, int dw, const unsigned char *src, int sw, int num)
{
   int si, di;

   // safety check
   if (dw < 3 || sw < 3 || dst == NULL || src == NULL)
      return;

   num--;
   for (si = num * sw, di = num * dw; si >= 0; si -= sw, di -= dw)
   {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      dst[di + 2] = src[si    ];
      dst[di + 1] = src[si + 1];
      dst[di + 0] = src[si + 2];
#else
      // FIXME: This is untested, it may be wrong.
      dst[di - 3] = src[si - 3];
      dst[di - 2] = src[si - 2];
      dst[di - 1] = src[si - 1];
#endif
    }
}

void dumpJpeg(int fd, drmModeCrtc *crtc) {
    drmModeFB *fb;
    /* structure to retrieve FB later */
    struct drm_mode_map_dumb dumb_map;

    /* check framebuffer id */
    fb = drmModeGetFB(fd, crtc->buffer_id);
    if (fb == NULL)
    {
        fprintf(stderr, "Unable to get framebuffer for specified CRTC.\n");
        return;
    }
    fprintf(stdout, "Got framebuffer at CRTC: %d.\n", crtc->crtc_id);

    /* Now this is how we dump the framebuffer */
    dumb_map.handle = fb->handle;    
    dumb_map.offset = 0;
    void *ptr;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &dumb_map) == 0 &&
    (ptr = mmap(0, fb->pitch * fb->height, PROT_READ, MAP_SHARED, fd, dumb_map.offset)) != (void *)-1)
    {
        char name[80];
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        /* More stuff */

        FILE *outfile;           /* target file */
        JSAMPROW row_pointer[1]; /* pointer to JSAMPLE row[s] */
        int row_stride;

        cinfo.err = jpeg_std_error(&jerr);
        /* Now we can initialize the JPEG compression object. */
        jpeg_create_compress(&cinfo);

        snprintf(name, sizeof(name), "screenshot-%d.jpg", fb->fb_id);
        if ((outfile = fopen(name, "wb")) == NULL)
        {
            fprintf(stderr, "Unable to open %s\n", name);
            return;
        }
        fprintf(stdout,"FB depth is %u pitch %u.\n", fb->depth, fb->pitch);

        jpeg_stdio_dest(&cinfo, outfile);
        cinfo.image_width = fb->width; /* image width and height, in pixels */
        cinfo.image_height = fb->height;
        cinfo.input_components = 3; /* # of color components per pixel */
        cinfo.in_color_space = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 75, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        row_stride = fb->pitch; /* JSAMPLEs per row in image_buffer */
        while (cinfo.next_scanline < cinfo.image_height)
        {
            unsigned char row_buf[3 * cinfo.image_width];
            pix_conv(row_buf, 3, ptr +(cinfo.next_scanline * row_stride), 4, cinfo.image_width);
            row_pointer[0] = row_buf;
            (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);

        }

        jpeg_finish_compress(&cinfo);
        fclose(outfile);
        jpeg_destroy_compress(&cinfo);
        munmap(ptr, fb->pitch * fb->height);
    }
    drmModeFreeFB(fb);
}

int main(int argc, char **argv)
{
    drmModeRes *res;
    drmModeCrtc *crtc;    

    int fd;
    unsigned int i;
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        fprintf(stderr, "Unable to open DRM device.\n");
        return ENOENT;
    }
    /*retrieve resources */
    res = drmModeGetResources(fd);
    if (!res)
    {
        fprintf(stderr, "Unable to retrieve DRM resources (%d).\n", errno);
        return -errno;
    }

    fprintf(stdout, "There are %d connectors.\n", res->count_connectors);

    /*walkthrough the connectors*/
    for (i=0; i< res->count_connectors; i++) {
        drmModeConnector *connector = NULL;

        connector = drmModeGetConnector(fd,res->connectors[i]);
        if (!connector) continue;
        if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0 ) {
            drmModeEncoder * encoder = drmModeGetEncoder(fd, connector->encoder_id);
            if (!encoder) continue;            
            crtc = drmModeGetCrtc(fd,encoder->crtc_id);
            drmModeFreeEncoder(encoder);
            if (crtc) {
                fprintf(stdout,"Connector %d is connected to encoder %d CRTC %d.\n",connector->connector_id,connector->encoder_id, crtc->crtc_id);
                dumpJpeg(fd,crtc);
                drmModeFreeCrtc(crtc);
            }
        }		
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(res);
    close(fd);
}
