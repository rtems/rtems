/**
 * @file
 *
 * @brief Serial Peripheral Interface (SPI) Bus Implementation
 *
 * @ingroup SPIBus
 */

/*
 * Copyright (C) 2016, 2017 embedded brains GmbH & Co. KG
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dev/spi/spi.h>

#include <rtems/imfs.h>

#include <stdlib.h>
#include <string.h>

static void spi_bus_obtain(spi_bus *bus)
{
  rtems_recursive_mutex_lock(&bus->mutex);
}

static void spi_bus_release(spi_bus *bus)
{
  rtems_recursive_mutex_unlock(&bus->mutex);
}

static void spi_bus_set_defaults(spi_bus *bus, spi_ioc_transfer *msg)
{
  msg->cs_change = bus->cs_change;
  msg->cs = bus->cs;
  msg->bits_per_word = bus->bits_per_word;
  msg->mode = bus->mode;
  msg->speed_hz = bus->speed_hz;
  msg->delay_usecs = bus->delay_usecs;
}

static ssize_t spi_bus_read(
  rtems_libio_t *iop,
  void *buffer,
  size_t count
)
{
  spi_bus *bus = IMFS_generic_get_context_by_iop(iop);
  spi_ioc_transfer msg = {
    .len = count,
    .rx_buf = buffer
  };
  int err;

  spi_bus_obtain(bus);
  spi_bus_set_defaults(bus, &msg);
  err = (*bus->transfer)(bus, &msg, 1);
  spi_bus_release(bus);

  if (err == 0) {
    return msg.len;
  } else {
    rtems_set_errno_and_return_minus_one(-err);
  }
}

static ssize_t spi_bus_write(
  rtems_libio_t *iop,
  const void *buffer,
  size_t count
)
{
  spi_bus *bus = IMFS_generic_get_context_by_iop(iop);
  spi_ioc_transfer msg = {
    .len = (uint16_t) count,
    .tx_buf = buffer
  };
  int err;

  spi_bus_obtain(bus);
  spi_bus_set_defaults(bus, &msg);
  err = (*bus->transfer)(bus, &msg, 1);
  spi_bus_release(bus);

  if (err == 0) {
    return msg.len;
  } else {
    rtems_set_errno_and_return_minus_one(-err);
  }
}

static int spi_bus_ioctl(
  rtems_libio_t *iop,
  ioctl_command_t command,
  void *arg
)
{
  spi_bus *bus = IMFS_generic_get_context_by_iop(iop);
  int err;
  uint32_t previous;

  spi_bus_obtain(bus);

  switch (command) {
    case SPI_BUS_OBTAIN:
      spi_bus_obtain(bus);
      err = 0;
      break;
    case SPI_BUS_RELEASE:
      spi_bus_release(bus);
      err = 0;
      break;
    case SPI_IOC_RD_MODE:
      *(uint8_t *) arg = bus->mode;
      err = 0;
      break;
    case SPI_IOC_RD_MODE32:
      *(uint32_t *) arg = bus->mode;
      err = 0;
      break;
    case SPI_IOC_RD_LSB_FIRST:
      *(uint8_t *) arg = bus->lsb_first;
      err = 0;
      break;
    case SPI_IOC_RD_BITS_PER_WORD:
      *(uint8_t *) arg = bus->bits_per_word;
      err = 0;
      break;
    case SPI_IOC_RD_MAX_SPEED_HZ:
      *(uint32_t *) arg = bus->speed_hz;
      err = 0;
      break;
    case SPI_IOC_WR_MODE:
      previous = bus->mode;
      bus->mode = *(uint8_t *) arg;
      err = (*bus->setup)(bus);

      if (err != 0) {
        bus->mode = previous;
      }

      break;
    case SPI_IOC_WR_MODE32:
      previous = bus->mode;
      bus->mode = *(uint32_t *) arg;
      err = (*bus->setup)(bus);

      if (err != 0) {
        bus->mode = previous;
      }

      break;
    case SPI_IOC_WR_LSB_FIRST:
      previous = bus->lsb_first;
      bus->lsb_first = (*(uint8_t *) arg) != 0;
      err = (*bus->setup)(bus);

      if (err != 0) {
        bus->lsb_first = (bool) previous;
      }

      break;
    case SPI_IOC_WR_BITS_PER_WORD:
      previous = bus->bits_per_word;
      bus->bits_per_word = *(uint8_t *) arg;
      err = (*bus->setup)(bus);

      if (err != 0) {
        bus->bits_per_word = (uint8_t) previous;
      }

      break;
    case SPI_IOC_WR_MAX_SPEED_HZ:
      previous = bus->speed_hz;
      bus->speed_hz = *(uint32_t *) arg;
      err = (*bus->setup)(bus);

      if (err != 0) {
        bus->speed_hz = previous;
      }

      break;
    default:
      if (IOCBASECMD(command) == IOCBASECMD(_IOW(SPI_IOC_MAGIC, 0, 0))) {
        err = (*bus->transfer)(
          bus,
          arg,
          IOCPARM_LEN(command) / sizeof(struct spi_ioc_transfer)
        );
      } else if (bus->ioctl != NULL) {
        err = (*bus->ioctl)(bus, command, arg);
      } else {
        err = -EINVAL;
      }
      break;
  }

  spi_bus_release(bus);

  if (err == 0) {
    return 0;
  } else {
    rtems_set_errno_and_return_minus_one(-err);
  }
}

static const rtems_filesystem_file_handlers_r spi_bus_handler = {
  .open_h = rtems_filesystem_default_open,
  .close_h = rtems_filesystem_default_close,
  .read_h = spi_bus_read,
  .write_h = spi_bus_write,
  .ioctl_h = spi_bus_ioctl,
  .lseek_h = rtems_filesystem_default_lseek,
  .fstat_h = IMFS_stat,
  .ftruncate_h = rtems_filesystem_default_ftruncate,
  .fsync_h = rtems_filesystem_default_fsync_or_fdatasync,
  .fdatasync_h = rtems_filesystem_default_fsync_or_fdatasync,
  .fcntl_h = rtems_filesystem_default_fcntl,
  .kqfilter_h = rtems_filesystem_default_kqfilter,
  .mmap_h = rtems_filesystem_default_mmap,
  .poll_h = rtems_filesystem_default_poll,
  .readv_h = rtems_filesystem_default_readv,
  .writev_h = rtems_filesystem_default_writev
};

static void spi_bus_node_destroy(IMFS_jnode_t *node)
{
  spi_bus *bus;

  bus = IMFS_generic_get_context_by_node(node);
  (*bus->destroy)(bus);

  IMFS_node_destroy_default(node);
}

static const IMFS_node_control spi_bus_node_control = IMFS_GENERIC_INITIALIZER(
  &spi_bus_handler,
  IMFS_node_initialize_generic,
  spi_bus_node_destroy
);

int spi_bus_register(
  spi_bus *bus,
  const char *bus_path
)
{
  int rv;

  rv = IMFS_make_generic_node(
    bus_path,
    S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO,
    &spi_bus_node_control,
    bus
  );
  if (rv != 0) {
    (*bus->destroy)(bus);
  }

  return rv;
}

static int spi_bus_transfer_default(
  spi_bus *bus,
  const spi_ioc_transfer *msgs,
  uint32_t msg_count
)
{
  (void) bus;
  (void) msgs;
  (void) msg_count;

  return -EIO;
}

static int spi_bus_setup_default(
  spi_bus *bus
)
{
  (void) bus;

  return -EIO;
}

static int spi_bus_do_init(
  spi_bus *bus,
  void (*destroy)(spi_bus *bus)
)
{
  rtems_recursive_mutex_init(&bus->mutex, "SPI Bus");
  bus->transfer = spi_bus_transfer_default;
  bus->setup = spi_bus_setup_default;
  bus->destroy = destroy;
  bus->ioctl = NULL;
  bus->bits_per_word = 8;

  return 0;
}

void spi_bus_destroy(spi_bus *bus)
{
  rtems_recursive_mutex_destroy(&bus->mutex);
}

void spi_bus_destroy_and_free(spi_bus *bus)
{
  spi_bus_destroy(bus);
  free(bus);
}

int spi_bus_init(spi_bus *bus)
{
  memset(bus, 0, sizeof(*bus));

  return spi_bus_do_init(bus, spi_bus_destroy);
}

spi_bus *spi_bus_alloc_and_init(size_t size)
{
  spi_bus *bus = NULL;

  if (size >= sizeof(*bus)) {
    bus = calloc(1, size);
    if (bus != NULL) {
      int rv;

      rv = spi_bus_do_init(bus, spi_bus_destroy_and_free);
      if (rv != 0) {
        return NULL;
      }
    }
  }

  return bus;
}
