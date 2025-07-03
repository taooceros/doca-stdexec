#pragma once
#include "doca_rdma.h"
#include <memory>
#ifndef DOCA_STDEXEC_DEVICE_HPP
#define DOCA_STDEXEC_DEVICE_HPP

#include <doca_dev.h>
#include <doca_types.h>

#include "common.hpp"

namespace doca_stdexec {

struct doca_dev_deleter {
  void operator()(doca_dev *dev) { doca_dev_close(dev); }
};

struct Device : public std::enable_shared_from_this<Device> {
  std::unique_ptr<doca_dev, doca_dev_deleter> device;

  Device(doca_dev *device) : device(device) {}

  static auto open_from_pci(const char *pci_addr) {
    return open_from_criteria([pci_addr](doca_devinfo *devinfo) {
      char pci_addr_str[DOCA_DEVINFO_PCI_ADDR_SIZE];
      doca_devinfo_get_pci_addr_str(devinfo, pci_addr_str);
      return strcmp(pci_addr_str, pci_addr) == 0;
    });
  }

  static auto open_from_ib_name(const char *ib_name) {
    return open_from_criteria([ib_name](doca_devinfo *devinfo) {
      char ib_name_str[DOCA_DEVINFO_IBDEV_NAME_SIZE];
      doca_devinfo_get_ibdev_name(devinfo, ib_name_str,
                                  DOCA_DEVINFO_IBDEV_NAME_SIZE);
      return strcmp(ib_name_str, ib_name) == 0;
    });
  }

  static Device open_from_criteria(auto criteria) {
    doca_dev *dev;
    doca_devinfo **devinfos;
    uint32_t num_devinfos;

    auto status = doca_devinfo_create_list(&devinfos, &num_devinfos);
    check_error(status, "Failed to create device info list: %d\n");

    for (uint32_t i = 0; i < num_devinfos; i++) {
      auto *devinfo = devinfos[i];

      if (criteria(devinfo)) {
        auto status = doca_dev_open(devinfo, &dev);
        check_error(status, "Failed to open device from devinfo: %d\n");
        doca_devinfo_destroy_list(devinfos);
        return Device(dev);
      }
    }

    doca_devinfo_destroy_list(devinfos);
    printf("No device found with criteria\n");
    exit(1);
  }

  ~Device() = default;

  doca_dev *get() const noexcept { return device.get(); }
};

} // namespace doca_stdexec

#endif