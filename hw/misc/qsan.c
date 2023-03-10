#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-core.h"
#include "exec/memory.h"
#include "qom/object.h"
#include "hw/pci/pci_device.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "exec/address-spaces.h"
#include "sysemu/hostmem.h"
#include "hw/acpi/erst.h"
#include "sysemu/runstate.h"
#include "trace.h"
#include "qsan.h"
#include "qapi/visitor.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>

struct QsanMemoryRegion {
    uint64_t start;
    uint64_t size;

    LIST_ENTRY(QsanMemoryRegion) pointers;
};

struct QsanPoison {
    LIST_HEAD(list, QsanMemoryRegion) head;
};

struct QsanCommand {
    uint8_t type;

    uint64_t start;
    uint64_t size;
};

typedef struct QsanState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion portio;
} QsanState;

struct QsanPoison poison_list;
bool initialized = false;

void qsan_validate_access(CPUArchState *env, hwaddr addr,
                          MMUAccessType access_type) {

    struct QsanMemoryRegion *region;

    LIST_FOREACH(region, &poison_list.head, pointers) {
        uint64_t start = region->start;
        uint64_t end = region->start + region->size;

        if (addr >= start && addr <= end) {
            // We are trying to access a poisoned memory area. Pause execution.
            vm_stop(RUN_STATE_PAUSED);
        }
    }
}

#define TYPE_PCI_QSAN_DEV "qsan"
#define PCI_QSAN_DEV(obj) OBJECT_CHECK(QsanState, (obj), TYPE_PCI_QSAN_DEV)

static uint64_t pci_read(void *opaque, hwaddr addr, unsigned size) {
    // QsanState *d = opaque;

    // if (addr == 0)
    //     return d->buf[d->pos++];
    // else
    //     return d->buflen;
    return 0;
}

static void pci_qsan_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
    if (!val || !initialized)
        return;

    struct QsanCommand command;
    cpu_physical_memory_read(val, &command, sizeof(struct QsanCommand));

    struct QsanMemoryRegion *item = malloc(sizeof(struct QsanMemoryRegion));
    item->start = command.start;
    item->size = command.size;

    LIST_INSERT_HEAD(&poison_list.head, item, pointers);
}

static const MemoryRegionOps pci_qsandev_mmio_ops = {
    .read = pci_read,
    .write = pci_qsan_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pci_qsan_realize(PCIDevice *pci_dev, Error **errp) {
    QsanState *d = PCI_QSAN_DEV(pci_dev);

    uint8_t *pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 0; // no interrupt pin

    memory_region_init_io(&d->mmio, OBJECT(d), &pci_qsandev_mmio_ops, d,
                          "pci-qsan-mmio", 128);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    LIST_INIT(&poison_list.head);
    initialized = true;

    printf("QSAN: Initialized!\n");
}

static void pci_qsan_uninit(PCIDevice *dev) {
    printf("QSAN: Uninitialized PCI device\n");
}

static void pci_qsan_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_qsan_realize;
    k->exit = pci_qsan_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = 0x6969;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;

    dc->desc = "Qemu Sanitizer (QSAN)";

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    printf("QSAN: Initialized PCI class\n");
}

static void pci_qsan_instance_init(Object *obj) {
    printf("QSAN: Initialized PCI device\n");
}

static void pci_qsan_register_types(void) {
    static InterfaceInfo interfaces[] = {
        {INTERFACE_CONVENTIONAL_PCI_DEVICE},
        {INTERFACE_PCIE_DEVICE},
        {},
    };

    static const TypeInfo edu_info = {
        .name = TYPE_PCI_QSAN_DEV,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(QsanState),
        .instance_init = pci_qsan_instance_init,
        .class_init = pci_qsan_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&edu_info);
}

type_init(pci_qsan_register_types)
