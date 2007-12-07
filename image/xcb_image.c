/* Copyright (C) 2007 Bart Massey
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Except as contained in this notice, the names of the authors or their
 * institutions shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization from the authors.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xcb_aux.h>
#include "xcb_bitops.h"
#include "xcb_image.h"
#include "xcb_pixel.h"


static xcb_format_t *
find_format_by_depth (const xcb_setup_t *setup, uint8_t depth)
{ 
  xcb_format_t *fmt = xcb_setup_pixmap_formats(setup);
  xcb_format_t *fmtend = fmt + xcb_setup_pixmap_formats_length(setup);
  for(; fmt != fmtend; ++fmt)
      if(fmt->depth == depth)
	  return fmt;
  return 0;
}


static xcb_image_format_t
effective_format(xcb_image_format_t format, uint8_t bpp)
{
    if (format == XCB_IMAGE_FORMAT_Z_PIXMAP && bpp != 1)
	    return format;
    return XCB_IMAGE_FORMAT_XY_PIXMAP;
}


static int
format_valid (uint8_t depth, uint8_t bpp, uint8_t unit,
	      xcb_image_format_t format, uint8_t xpad)
{
  xcb_image_format_t  ef = effective_format(format, bpp);
  if (depth > bpp || bpp > unit)
      return 0;
  switch(ef) {
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      switch(unit) {
      case 8:
      case 16:
      case 32:
	  break;
      default:
	  return 0;
      }
      if (xpad < bpp)
	  return 0;
      switch (xpad) {
      case 8:
      case 16:
      case 32:
	  break;
      default:
	  return 0;
      }
      break;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      switch (bpp) {
      case 4:
	  if (unit != 8)
	      return 0;
	  break;
      case 8:
      case 16:
      case 24:
      case 32:
	  if (unit != bpp)
	      return 0;
	  break;
      default:
	  return 0;
      }
      break;
  default:
      return 0;
  }
  return 1;
}


static int
image_format_valid (xcb_image_t *image) {
    return format_valid(image->depth,
			image->bpp,
			image->unit,
			image->format,
			image->scanline_pad);
}


void
xcb_image_annotate (xcb_image_t *image)
{
  xcb_image_format_t  ef = effective_format(image->format, image->bpp);
  switch (ef) {
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      image->plane_mask = xcb_mask(image->depth);
      image->stride = xcb_roundup(image->width, image->scanline_pad) >> 3;
      image->size = image->height * image->stride * image->depth;
      break;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      image->plane_mask = 0;
      image->stride = xcb_roundup((uint32_t)image->width *
				  (uint32_t)image->bpp,
				  image->scanline_pad) >> 3;
      image->size = image->height * image->stride;
      break;
  default:
      assert(0);
  }
}


xcb_image_t *
xcb_image_create_native (xcb_connection_t *  c,
			 uint16_t            width,
			 uint16_t            height,
			 xcb_image_format_t  format,
			 uint8_t             depth,
			 void *              base,
			 uint32_t            bytes,
			 uint8_t *           data)
{
  const xcb_setup_t *  setup = xcb_get_setup(c);
  xcb_format_t *       fmt;
  xcb_image_format_t   ef = format;
  
  if (ef == XCB_IMAGE_FORMAT_Z_PIXMAP && depth == 1)
      ef = XCB_IMAGE_FORMAT_XY_PIXMAP;
  switch (ef) {
  case XCB_IMAGE_FORMAT_XY_BITMAP:
      if (depth != 1)
	  return 0;
      /* fall through */
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      if (depth > 1) {
	  fmt = find_format_by_depth(setup, depth);
	  if (!fmt)
	      return 0;
      }
      return xcb_image_create(width, height, format,
			      setup->bitmap_format_scanline_pad,
			      depth, depth, setup->bitmap_format_scanline_unit,
			      setup->image_byte_order,
			      setup->bitmap_format_bit_order,
			      base, bytes, data);
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      fmt = find_format_by_depth(setup, depth);
      if (!fmt)
	  return 0;
      return xcb_image_create(width, height, format,
			      fmt->scanline_pad,
			      fmt->depth, fmt->bits_per_pixel, 0,
			      setup->image_byte_order,
			      XCB_IMAGE_ORDER_MSB_FIRST,
			      base, bytes, data);
  default:
      assert(0);
  }
  assert(0);
}


xcb_image_t *
xcb_image_create (uint16_t           width,
		  uint16_t           height,
		  xcb_image_format_t format,
		  uint8_t            xpad,
		  uint8_t            depth,
		  uint8_t            bpp,
		  uint8_t            unit,
		  xcb_image_order_t  byte_order,
		  xcb_image_order_t  bit_order,
		  void *             base,
		  uint32_t           bytes,
		  uint8_t *          data)
{
  xcb_image_t *  image;

  if (unit == 0) {
      switch (format) {
      case XCB_IMAGE_FORMAT_XY_BITMAP:
      case XCB_IMAGE_FORMAT_XY_PIXMAP:
	  unit = 32;
	  break;
      case XCB_IMAGE_FORMAT_Z_PIXMAP:
	  if (bpp == 1) {
	      unit = 32;
	      break;
	  }
	  if (bpp < 8) {
	      unit = 8;
	      break;
	  }
	  unit = bpp;
	  break;
      }
  }
  if (!format_valid(depth, bpp, unit, format, xpad))
      return 0;
  image = (xcb_image_t *)malloc(sizeof(*image));
  if (image == 0)
      return 0;
  image->width = width;
  image->height = height;
  image->format = format;
  image->scanline_pad = xpad;
  image->depth = depth;
  image->bpp = bpp;
  image->unit = unit;
  image->byte_order = byte_order;
  image->bit_order = bit_order;
  xcb_image_annotate(image);

  /*
   * Ways this function can be called:
   *   * with data: we fail if bytes isn't
   *     large enough, else leave well enough alone.
   *   * with base and !data: if bytes is zero, we
   *     default; otherwise we fail if bytes isn't
   *     large enough, else fill in data
   *   * with !base and !data: we malloc storage
   *     for the data, save that address as the base,
   *     and fail if malloc does.
   *
   * When successful, we establish the invariant that data
   * points at sufficient storage that may have been
   * supplied, and base is set iff it should be
   * auto-freed when the image is destroyed.
   * 
   * Except as a special case when base = 0 && data == 0 &&
   * bytes == ~0 we just return the image structure and let
   * the caller deal with getting the allocation right.
   */
  if (!base && !data && bytes == ~0)
      return image;
  if (!base && data && bytes == 0)
      bytes = image->size;
  image->base = base;
  image->data = data;
  if (!image->data) {
      if (image->base) {
	  image->data = image->base;
      } else {
	  bytes = image->size;
	  image->base = malloc(bytes);
	  image->data = image->base;
      }
  }
  if (!image->data || bytes < image->size) {
      free(image);
      return 0;
  }
  return image;
}


void
xcb_image_destroy (xcb_image_t *image)
{
  if (image->base)
      free (image->base);
  free (image);
}


xcb_image_t *
xcb_image_get (xcb_connection_t *  conn,
	       xcb_drawable_t      draw,
	       int16_t             x,
	       int16_t             y,
	       uint16_t            width,
	       uint16_t            height,
	       uint32_t            plane_mask,
	       xcb_image_format_t  format)
{
  xcb_get_image_cookie_t   image_cookie;
  xcb_get_image_reply_t *  imrep;
  xcb_image_t *            image = 0;
  uint32_t                 bytes;
  uint8_t *                data;

  image_cookie = xcb_get_image(conn, format, draw, x, y,
			       width, height, plane_mask);
  imrep = xcb_get_image_reply(conn, image_cookie, 0);
  if (!imrep)
      return 0;
  bytes = xcb_get_image_data_length(imrep);
  data = xcb_get_image_data(imrep);
  switch (format) {
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      plane_mask &= xcb_mask(imrep->depth);
      if (plane_mask != xcb_mask(imrep->depth)) {
	  xcb_image_t *  tmp_image =
	    xcb_image_create_native(conn, width, height, format,
				    imrep->depth, 0, 0, 0);
	  int            i;
	  uint32_t       rpm = plane_mask;
	  uint8_t *      src_plane = image->data;
	  uint8_t *      dst_plane = tmp_image->data;
	  uint32_t       size = image->height * image->stride;
	  
	  if (!tmp_image) {
	      free(imrep);
	      return 0;
	  }
	  if (tmp_image->bit_order == XCB_IMAGE_ORDER_MSB_FIRST)
	      rpm = xcb_bit_reverse(plane_mask, imrep->depth);
	  for (i = 0; i < imrep->depth; i++) {
	      if (rpm & 1) {
		  memcpy(dst_plane, src_plane, size);
		  src_plane += size;
	      } else {
		  memset(dst_plane, 0, size);
	      }
	      dst_plane += size;
	  }
	  tmp_image->plane_mask = plane_mask;
	  image = tmp_image;
	  free(imrep);
	  break;
      }
      /* fall through */
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      image = xcb_image_create_native(conn, width, height, format,
				      imrep->depth, imrep, bytes, data);
      if (!image)
	  free(imrep);
      break;
  default:
      assert(0);
  }
  assert(bytes == image->size);
  return image;
}


xcb_image_t *
xcb_image_native (xcb_connection_t *  c,
		  xcb_image_t *       image,
		  int                 convert)
{
  xcb_image_t *        tmp_image = 0;
  const xcb_setup_t *  setup = xcb_get_setup(c);
  xcb_format_t *       fmt = 0;
  xcb_image_format_t   ef = effective_format(image->format, image->bpp);
  uint8_t              bpp = 1;

  if (image->depth > 1 || ef == XCB_IMAGE_FORMAT_Z_PIXMAP) {
      fmt = find_format_by_depth(setup, image->depth);
      /* XXX For now, we don't do depth conversions, even
	 for xy-pixmaps */
      if (!fmt)
	  return 0;
      bpp = fmt->bits_per_pixel;
  }
  switch (ef) {
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      if (setup->bitmap_format_scanline_unit != image->unit ||
	  setup->bitmap_format_scanline_pad != image->scanline_pad ||
	  setup->image_byte_order != image->byte_order ||
	  setup->bitmap_format_bit_order != image->bit_order ||
	  bpp != image->bpp) {
	  if (!convert)
	      return 0;
	  tmp_image =
	      xcb_image_create(image->width, image->height, image->format,
			       setup->bitmap_format_scanline_pad,
			       image->depth, bpp,
			       setup->bitmap_format_scanline_unit,
			       setup->image_byte_order,
			       setup->bitmap_format_bit_order,
			       0, 0, 0);
	  if (!tmp_image)
	      return 0;
      }
      break;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      if (fmt->scanline_pad != image->scanline_pad ||
	  setup->image_byte_order != image->byte_order ||
	  bpp != image->bpp) {
	  if (!convert)
	      return 0;
	  tmp_image =
	      xcb_image_create(image->width, image->height, image->format,
			       fmt->scanline_pad,
			       image->depth, bpp, 0,
			       setup->image_byte_order,
			       XCB_IMAGE_ORDER_MSB_FIRST,
			       0, 0, 0);
	  if (!tmp_image)
	      return 0;
      }
      break;
  default:
      assert(0);
  }
  if (tmp_image) {
      if (!xcb_image_convert(image, tmp_image)) {
	  xcb_image_destroy(tmp_image);
	  return 0;
      }
      image = tmp_image;
  }
  return image;
}


xcb_void_cookie_t
xcb_image_put (xcb_connection_t *  conn,
	       xcb_drawable_t      draw,
	       xcb_gcontext_t      gc,
	       xcb_image_t *       image,
	       int16_t             x,
	       int16_t             y,
	       uint8_t             left_pad)
{
  return xcb_put_image(conn, image->format, draw, gc,
		       image->width, image->height,
		       x, y, left_pad,
		       image->depth,
		       image->size,
		       image->data);
}



/*
 * Shm stuff
 */

xcb_image_t *
xcb_image_shm_put (xcb_connection_t *      conn,
		   xcb_drawable_t          draw,
		   xcb_gcontext_t          gc,
		   xcb_image_t *           image,
		   xcb_shm_segment_info_t  shminfo,
		   int16_t                 src_x,
		   int16_t                 src_y,
		   int16_t                 dest_x,
		   int16_t                 dest_y,
		   uint16_t                src_width,
		   uint16_t                src_height,
		   uint8_t                 send_event)
{
  if (!xcb_image_native(conn, image, 0))
      return 0;
  if (!shminfo.shmaddr)
      return 0;
  xcb_shm_put_image(conn, draw, gc,
		    image->width, image->height,
		    src_x, src_y, src_width, src_height,
		    dest_x, dest_y,
		    image->depth, image->format,
		    send_event, 
		    shminfo.shmseg,
		    image->data - shminfo.shmaddr);
  return image;
}


int
xcb_image_shm_get (xcb_connection_t *      conn,
		   xcb_drawable_t          draw,
		   xcb_image_t *           image,
		   xcb_shm_segment_info_t  shminfo,
		   int16_t                 x,
		   int16_t                 y,
		   uint32_t                plane_mask)
{
  xcb_shm_get_image_reply_t *  setup;
  xcb_shm_get_image_cookie_t   cookie;
  xcb_generic_error_t *        err = 0;

  if (!shminfo.shmaddr)
      return 0;
  cookie = xcb_shm_get_image(conn, draw,
			     x, y,
			     image->width, image->height,
			     plane_mask,
			     image->format,
			     shminfo.shmseg,
			     image->data - shminfo.shmaddr);
  setup = xcb_shm_get_image_reply(conn, cookie, &err);
  if (err) {
      fprintf(stderr, "ShmGetImageReply error %d\n", (int)err->error_code);
      free(err);
      return 0;
  } else {
      free (setup);
      return 1;
  }
}


/* GetPixel/PutPixel */

/* XXX this is the most hideously done cut-and-paste
   to below.  Any bugs fixed there should be fixed here
   and vice versa. */
void
xcb_image_put_pixel (xcb_image_t *image,
		     uint32_t x,
		     uint32_t y,
		     uint32_t pixel)
{
  uint8_t *row;

  if (x > image->width || y > image->height)
      return;
  row = image->data + (y * image->stride);
  switch (image->format) {
  case XCB_IMAGE_FORMAT_XY_BITMAP:
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      /* block */ {
	  int  p;
	  uint32_t   plane_mask = image->plane_mask;
	  uint8_t *  plane = row;
	  uint32_t   ulog = image->bpp >> 4;
	  uint32_t   unit = (x >> 3) & ~xcb_mask(ulog);
	  uint32_t   byte = (x >> 3) & xcb_mask(ulog);
	  uint32_t   bit = x & 7;

	  if (image->byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
	      byte = xcb_mask(ulog) - byte;
	  if (image->bit_order == XCB_IMAGE_ORDER_MSB_FIRST) {
	      bit = 7 - bit;
	  } else {
	      pixel = xcb_bit_reverse(pixel, image->bpp);
	      plane_mask = xcb_bit_reverse(plane_mask, image->bpp);
	  }
	  for (p = 0; p < image->bpp; p++) {
	      if (plane_mask & 1) {
		  uint8_t *  bp = plane + (unit | byte);
		  uint8_t    m = 1 << bit;
		  uint8_t    p = (pixel & 1) << bit;
		  *bp = (*bp & ~m) | p;
	      }
	      plane += image->stride * image->height;
	      pixel >>= 1;
	      plane_mask >>= 1;
	  }
      }
      break;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      switch (image->bpp) {
      uint32_t   mask;
      case 4:
	  mask = 0xf;
	  pixel &= 0xf;
	  if ((x & 1) ==
	      (image->byte_order == XCB_IMAGE_ORDER_MSB_FIRST)) {
	      pixel <<= 4;
	      mask <<= 4;
	  }
	  row[x >> 1] = (row[x >> 1] & ~mask) | pixel;
	  break;
      case 8:
	  row[x] = pixel;
	  break;
      case 16:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      row[x << 1] = pixel;
	      row[(x << 1) + 1] = pixel >> 8;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      row[x << 1] = pixel >> 8;
	      row[(x << 1) + 1] = pixel;
	      break;
	  }
	  break;
      case 24:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      row[x * 3] = pixel;
	      row[x * 3 + 1] = pixel >> 8;
	      row[x * 3 + 2] = pixel >> 16;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      row[x * 3] = pixel >> 16;
	      row[x * 3 + 1] = pixel >> 8;
	      row[x * 3 + 2] = pixel;
	      break;
	  }
	  break;
      case 32:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      row[x << 2] = pixel;
	      row[(x << 2) + 1] = pixel >> 8;
	      row[(x << 2) + 2] = pixel >> 16;
	      row[(x << 2) + 3] = pixel >> 24;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      row[x << 2] = pixel >> 24;
	      row[(x << 2) + 1] = pixel >> 16;
	      row[(x << 2) + 2] = pixel >> 8;
	      row[(x << 2) + 3] = pixel;
	      break;
	  }
	  break;
      default:
	  assert(0);
      }
      break;
  default:
      assert(0);
  }
}


/* XXX this is the most hideously done cut-and-paste
   from above.  Any bugs fixed there should be fixed here
   and vice versa. */
uint32_t
xcb_image_get_pixel (xcb_image_t *image,
		     uint32_t x,
		     uint32_t y)
{
  uint32_t pixel = 0;
  uint8_t *row;

  assert(x < image->width && y < image->height);
  row = image->data + (y * image->stride);
  switch (image->format) {
  case XCB_IMAGE_FORMAT_XY_BITMAP:
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      /* block */ {
	  int  p;
	  uint32_t   plane_mask = image->plane_mask;
	  uint8_t *  plane = row;
	  uint32_t   ulog = image->bpp >> 4;
	  uint32_t   unit = (x >> 3) & ~xcb_mask(ulog);
	  uint32_t   byte = (x >> 3) & xcb_mask(ulog);
	  uint32_t   bit = x & 7;

	  if (image->byte_order == XCB_IMAGE_ORDER_MSB_FIRST)
	      byte = xcb_mask(ulog) - byte;
	  if (image->bit_order == XCB_IMAGE_ORDER_MSB_FIRST) {
	      bit = 7 - bit;
	  } else {
	      plane_mask = xcb_bit_reverse(plane_mask, image->bpp);
	  }
	  for (p = 0; p < image->bpp; p++) {
	      pixel <<= 1;
	      if (plane_mask & 1) {
		  uint8_t *  bp = plane + (unit | byte);
		  pixel |= (*bp >> bit) & 1;
	      }
	      plane += image->stride * image->height;
	      plane_mask >>= 1;
	  }
	  if (image->bit_order == XCB_IMAGE_ORDER_LSB_FIRST)
	      pixel = xcb_bit_reverse(pixel, image->bpp);
      }
      return pixel;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      switch (image->bpp) {
      case 4:
	  if ((x & 1) == (image->byte_order == XCB_IMAGE_ORDER_MSB_FIRST))
	      return row[x >> 1] >> 4;
	  return row[x >> 1] & 0xf;
      case 8:
	  return row[x];
      case 16:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      pixel = row[x << 1];
	      pixel |= row[(x << 1) + 1] << 8;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      pixel = row[x << 1] << 8;
	      pixel |= row[(x << 1) + 1];
	      break;
	  }
	  break;
      case 24:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      pixel = row[x * 3];
	      pixel |= row[x * 3 + 1] << 8;
	      pixel |= row[x * 3 + 2] << 16;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      pixel = row[x * 3] << 16;
	      pixel |= row[x * 3 + 1] << 8;
	      pixel |= row[x * 3 + 2];
	      break;
	  }
	  break;
      case 32:
	  switch (image->byte_order) {
	  case XCB_IMAGE_ORDER_LSB_FIRST:
	      pixel = row[x << 2];
	      pixel |= row[(x << 2) + 1] << 8;
	      pixel |= row[(x << 2) + 2] << 16;
	      pixel |= row[(x << 2) + 3] << 24;
	      break;
	  case XCB_IMAGE_ORDER_MSB_FIRST:
	      pixel = row[x << 2] << 24;
	      pixel |= row[(x << 2) + 1] << 16;
	      pixel |= row[(x << 2) + 2] << 8;
	      pixel |= row[(x << 2) + 3];
	      break;
	  }
	  break;
      default:
	  assert(0);
      }
      return pixel;
  default:
      assert(0);
  }
}


/*
 * (Adapted from libX11.)
 *
 * xcb_create_pixmap_from_bitmap_data: Routine to make a pixmap of
 *      given depth from user supplied bitmap data.
 *	D is any drawable on the same screen that the pixmap will be used in.
 *	Data is a pointer to the bit data, and 
 *	width & height give the size in bits of the pixmap.
 *
 * The following format is assumed for data:
 *
 *    format=XY (will use XYPixmap for depth 1 and XYBitmap for larger)
 *    bit_order=LSBFirst
 *    padding=8
 *    bitmap_unit=8
 */  
xcb_pixmap_t
xcb_create_pixmap_from_bitmap_data (xcb_connection_t *  display,
				    xcb_drawable_t      d,
				    uint8_t *           data,
				    uint32_t            width,
				    uint32_t            height,
				    uint32_t            depth,
				    uint32_t            fg,
				    uint32_t            bg,
				    xcb_gcontext_t *    gcp)
{
  xcb_pixmap_t        pix;
  xcb_image_t *       image;
  xcb_image_t *       final_image;
  xcb_image_format_t  format = XCB_IMAGE_FORMAT_XY_PIXMAP;
  xcb_gcontext_t gc;
  uint32_t mask = 0;
  xcb_params_gc_t gcv;

  if (depth > 1)
      format = XCB_IMAGE_FORMAT_XY_BITMAP;
  image = xcb_image_create(width, height, format,
			   8, 1, 1, 8,
			   XCB_IMAGE_ORDER_LSB_FIRST,
			   XCB_IMAGE_ORDER_LSB_FIRST,
			   0, 0, data);
  if (!image)
      return 0;
  final_image = xcb_image_native(display, image, 1);
  if (!final_image) {
      xcb_image_destroy(image);
      return 0;
  }
  pix = xcb_generate_id(display);
  xcb_create_pixmap(display, depth, pix, d, width, height);
  gc = xcb_generate_id(display);
  XCB_AUX_ADD_PARAM(&mask, &gcv, foreground, fg);
  XCB_AUX_ADD_PARAM(&mask, &gcv, background, bg);
  xcb_aux_create_gc(display, gc, pix, mask, &gcv);
  xcb_image_put(display, pix, gc, final_image, 0, 0, 0);
  if (final_image != image)
      xcb_image_destroy(final_image);
  xcb_image_destroy(image);
  if (gcp)
      *gcp = gc;
  else
      xcb_free_gc(display, gc);
  return pix;
}


/* Thanks to Keith Packard <keithp@keithp.com> for this code */
static void 
swap_image(uint8_t *	     src,
           uint32_t 	     src_stride,
	   uint8_t *	     dst,
	   uint32_t 	     dst_stride,
	   uint8_t const *   byte_order,
	   int		     unit_bytes,
	   uint16_t 	     height,
	   int		     bitswap,
	   int               nibbleswap)
{
  while (height--) {
      uint32_t    minor = 0;
      uint32_t    major = 0;
      uint32_t    s;

      for (s = 0; s < src_stride; s++) {
	  uint8_t   b;
	  uint32_t  d = major + byte_order[minor];

	  if (d > dst_stride)
	      continue;

	  b = src[s];
	  if (bitswap)
	      b = xcb_bit_reverse(b, 8);
	  if (nibbleswap)
	      b = (b << 4) | (b >> 4);
	  dst[d] = b;

	  if (++minor == unit_bytes) 
	  {
	      minor = 0; 
	      major += unit_bytes;
	  }
      }
      src += src_stride;
      dst += dst_stride;
  }
}

/* Note that all of these permutations are self-inverse.
   Applying them twice yields the identity permutation, i.e p*p = i
   This means that we only have to care about the
   source and destination size and whether they mismatch, not
   the actual endiannesses. */
static uint8_t const forward_order[4] = {0, 1, 2, 3};
static uint8_t const reverse_order[4] = {3, 2, 1, 0};
static uint8_t const reverse_word_order[4] = {2, 3, 0, 1};

static uint8_t const *
conversion_byte_order(xcb_image_t *src, xcb_image_t *dst)
{
    uint8_t  nbytes = src->unit >> 3;
    
    if (src->byte_order == dst->byte_order)
	return forward_order;
    if (nbytes >= 1 && nbytes <= 4)
	return &reverse_order[4 - nbytes];
    return forward_order;
}


#define R1 forward_order
#define R2 reverse_word_order
#define R4 reverse_order
#define W4 reverse_word_order

static uint8_t const * const bbo_reverse[3][3] =
        /* 8  16  32*/
  /*8*/ {{R1, R2, R4},
 /*16*/  {R2, R2, W4},
 /*32*/  {R4, W4, R4}};

static uint8_t const *
bitmap_conversion_byte_order(xcb_image_t *src, xcb_image_t *dst)
{
    uint8_t  srclog = src->unit >> 4;
    uint8_t  dstlog = dst->unit >> 4;
    int sbo = src->byte_order;
    int dbo = dst->byte_order;

    if (srclog == 0)
	sbo = XCB_IMAGE_ORDER_LSB_FIRST;
    if (dstlog == 0)
	dbo = XCB_IMAGE_ORDER_LSB_FIRST;
    if (dbo == sbo)
	return forward_order;
    return bbo_reverse[srclog][dstlog];
}


xcb_image_t *
xcb_image_convert (xcb_image_t *  src,
		   xcb_image_t *  dst)
{
  uint32_t            x;
  uint32_t            y;
  int                 format_compatible = 0;
  int                 bitmap = 0;
  xcb_image_format_t  ef = effective_format(src->format, src->bpp);

  /* Things will go horribly wrong here if a bad
     image is passed in, so we check some things
     up front just to be nice. */
  assert(image_format_valid(src));
  assert(image_format_valid(dst));
  if (src->depth != dst->depth ||
      src->width != dst->width ||
      src->height != dst->height)
      return 0;
  switch (ef) {
  case XCB_IMAGE_FORMAT_XY_PIXMAP:
      bitmap = src->depth == 1;
      format_compatible = src->format == dst->format || bitmap;
      /* Case: Formats are identical.  Just copy. */
      if (format_compatible &&
	  src->bpp == dst->bpp &&
	  src->unit == dst->unit &&
	  src->scanline_pad == dst->scanline_pad &&
	  src->byte_order == dst->byte_order &&
	  src->bit_order == dst->bit_order) {
	  memcpy(dst->data, src->data, src->size);
	  return dst;
      }
      break;
  case XCB_IMAGE_FORMAT_Z_PIXMAP:
      format_compatible = src->format == dst->format;
      /* Case: Formats are identical.  Just copy. */
      if (format_compatible &&
	  src->bpp == dst->bpp &&
	  src->byte_order == dst->byte_order) {
	  memcpy(dst->data, src->data, src->size);
	  return dst;
      }
      break;
  default:
      assert(0);
  }
  /* Case: Bitmap scanline units are always compatible.  Copy and convert. */
  if (bitmap) {
      uint8_t const * const
	       byte_order = bitmap_conversion_byte_order(src, dst);
      int      bitswap = src->bit_order != dst->bit_order;
      uint8_t  unit = src->unit;
      
      if (dst->unit < unit)
	  unit = dst->unit;
      swap_image(src->data, src->stride,
		     dst->data, dst->stride,
		     byte_order, unit >> 3,
		     src->height, bitswap, 0);
      return dst;
  }
  /* Case: Pixmap scanline units are identical.  Copy and convert. */
  if (format_compatible && src->bpp == dst->bpp) {
      uint8_t const * const
	   byte_order = conversion_byte_order(src, dst);
      int  bitswap = src->bit_order != dst->bit_order;
      int  nibbleswap = src->byte_order != dst->byte_order &&
                        src->bpp == 4;
      swap_image(src->data, src->stride,
		     dst->data, dst->stride,
		     byte_order, src->unit >> 3,
		     src->height, bitswap, nibbleswap);
      return dst;
  }

  /* General case: Slow pixel copy. Should we optimize
     Z24<->Z32 copies of either endianness? */
  for (y = 0; y < src->height; y++) {
      for (x = 0; x < src->width; x++) {
	  uint32_t  pixel = xcb_image_get_pixel(src, x, y);
	  xcb_image_put_pixel(dst, x, y, pixel);
      }
  }
  return dst;
}
