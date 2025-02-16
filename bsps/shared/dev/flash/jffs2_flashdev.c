/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Copyright (C) 2024 On-Line Applications Research Corporation (OAR)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dev/flash/jffs2_flashdev.h>
#include <rtems/jffs2.h>
#include <rtems/libio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  rtems_jffs2_flash_control super;
  FILE *handle;
  rtems_jffs2_mount_data *mount_data;
} flash_control;

static flash_control *get_flash_control( rtems_jffs2_flash_control *super )
{
  return (flash_control *) super;
}

static int do_read(
  rtems_jffs2_flash_control *super,
  uint32_t offset,
  unsigned char *buffer,
  size_t length
)
{
  int status;
  flash_control *self = get_flash_control( super );
  int fd = fileno(self->handle);

  status = lseek(fd, offset, SEEK_SET);
  if (status == -1) {
      return status;
  }

  status = read(fd, buffer, length);
  if (status == -1) {
      return status;
  }

  return 0;
}

static int do_write(
  rtems_jffs2_flash_control *super,
  uint32_t offset,
  const unsigned char *buffer,
  size_t length
)
{
  int status;
  flash_control *self = get_flash_control( super );
  int fd = fileno(self->handle);

  status = lseek(fd, offset, SEEK_SET);
  if (status == -1) {
      return status;
  }

  status = write(fd, buffer, length);
  if (status == -1) {
      return status;
  }

  return 0;
}

static int do_erase(
  rtems_jffs2_flash_control *super,
  uint32_t offset
)
{
  int status;
  flash_control *self = get_flash_control( super );
  int fd = fileno(self->handle);
  rtems_flashdev_region args;

  args.offset = offset;
  args.size = super->block_size;

  status = ioctl(fd, RTEMS_FLASHDEV_IOCTL_ERASE, &args);
  if (status == -1) {
      return status;
  }

  return 0;
}

static void do_destroy( rtems_jffs2_flash_control *super )
{
  flash_control *self = get_flash_control( super );
  fclose(self->handle);
  free(self->mount_data);
  free(self);
}

static int get_sector_size(int fd, uint32_t *size)
{
  rtems_flashdev_ioctl_sector_info sec_info = {0, };
  int status;

  status = ioctl(fd, RTEMS_FLASHDEV_IOCTL_SECTORINFO_BY_OFFSET, &sec_info);
  if (status == 0) {
    *size = sec_info.sector_info.size;
  }

  return status;
}

static int get_page_size(int fd, uint32_t *size)
{
  rtems_flashdev_ioctl_page_info page_info = {0, };
  int status;

  status = ioctl(fd, RTEMS_FLASHDEV_IOCTL_PAGEINFO_BY_OFFSET, &page_info);
  if (status == 0) {
    *size = page_info.page_info.size;
  }

  return status;
}

static int get_jedec_id(int fd, dev_t *dev_jedec_id)
{
  uint32_t jedec_id;
  int status = ioctl(fd, RTEMS_FLASHDEV_IOCTL_JEDEC_ID, &jedec_id);

  *dev_jedec_id = jedec_id;

  return status;
}

static int get_flash_type(int fd, rtems_flashdev_flash_type *flash_type)
{
  return ioctl(fd, RTEMS_FLASHDEV_IOCTL_TYPE, flash_type);
}

rtems_status_code jffs2_flashdev_mount(
  const char *flashdev_path,
  const char *mount_dir,
  rtems_flashdev_region *region,
  rtems_jffs2_compressor_control *compressor_control,
  bool read_only
)
{
  FILE *file;
  int fd;
  int status;
  rtems_jffs2_mount_data *mount_data;
  flash_control *instance;
  rtems_flashdev_flash_type flash_type;

  file = fopen(flashdev_path, read_only ? "r" : "r+");
  if (file == NULL) {
    return RTEMS_NO_MEMORY;
  }

  fd = fileno(file);
  status = ioctl(fd, RTEMS_FLASHDEV_IOCTL_REGION_SET, region);
  if (status) {
    fclose(file);
    return RTEMS_NOT_IMPLEMENTED;
  }

  mount_data = malloc(sizeof(*mount_data));
  if (mount_data == NULL) {
    fclose(file);
    return RTEMS_NO_MEMORY;
  }
  memset(mount_data, 0, sizeof(*mount_data));

  instance = malloc(sizeof(*instance));
  if (instance == NULL) {
    free(mount_data);
    fclose(file);
    return RTEMS_NO_MEMORY;
  }
  memset(instance, 0, sizeof(*instance));

  instance->handle = file;
  instance->mount_data = mount_data;
  mount_data->flash_control = &instance->super;
  mount_data->compressor_control = compressor_control;

  instance->super.read = do_read;
  instance->super.write = do_write;
  instance->super.erase = do_erase;
  instance->super.destroy = do_destroy;

  /* Get JEDEC ID, device_identifier is a 64bit dev_t */
  status = get_jedec_id(fd, &instance->super.device_identifier);
  if ( status != 0 ) {
    return status;
  }

  /* Set flash size from region size */
  instance->super.flash_size = region->size;

  /* Retrieve page size as sector/block size */
  status = get_sector_size(fd, &instance->super.block_size);
  if ( status != 0 ) {
    return status;
  }

  status = get_flash_type(fd, &flash_type);
  if ( status != 0 ) {
    return status;
  }

  /*
   * Write size should only be provided to JFFS2 for NAND flash since JFFS2 uses
   * a write size of 0 to indicate non-NAND flash to disable write buffers.
   */
  if (flash_type == RTEMS_FLASHDEV_NAND) {
    status = get_page_size(fd, &instance->super.write_size);
    if ( status != 0 ) {
      return status;
    }
  }

  status = mount(
    NULL,
    mount_dir,
    RTEMS_FILESYSTEM_TYPE_JFFS2,
    RTEMS_FILESYSTEM_READ_WRITE,
    mount_data
  );
  if ( status != 0 ) {
    return status;
  }

  return 0;
}
