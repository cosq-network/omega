#ifndef OMEGA_KERNEL_STORAGE_HPP
#define OMEGA_KERNEL_STORAGE_HPP

#include "std/cstdint.hpp"

namespace storage {

enum class DeviceType : uint8_t { Block, Optical, Removable, Virtual };
enum class Protocol : uint8_t {
    Memory,
    VirtioBlock,
    Nvme,
    AhciAta,
    Atapi,
    Sdhci,
    UsbMassStorage
};

enum class RequestType : uint8_t { Read, Write, Flush, Identify, Eject };

enum class Status : uint8_t {
    Success,
    InvalidRequest,
    OutOfBounds,
    NotReady,
    Timeout,
    DeviceRemoved,
    MediaChanged,
    Unsupported,
    ProtocolError,
    ResetRequired,
    DmaFailure,
    ReadOnly,
    IoError
};

enum DeviceFlags : uint32_t {
    DEVICE_REMOVABLE = 1u << 0,
    DEVICE_READ_ONLY = 1u << 1,
    DEVICE_FLUSH     = 1u << 2,
    DEVICE_HOTPLUG   = 1u << 3,
    DEVICE_OPTICAL   = 1u << 4,
    DEVICE_WRITABLE  = 1u << 5,
    DEVICE_BARRIER   = 1u << 6
};

enum RequestFlags : uint32_t {
    REQUEST_FUA       = 1u << 0,
    REQUEST_BARRIER   = 1u << 1,
    REQUEST_SYNC      = 1u << 2
};

struct Geometry {
    uint64_t total_blocks;
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint32_t max_transfer_blocks;
};

struct Device;
struct Request;

typedef void (*Completion)(Request* request, Status status, void* context);

struct Request {
    RequestType type;
    uint64_t lba;
    uint32_t block_count;
    void* buffer;
    uint32_t flags;
    Completion complete;
    void* context;
};

struct DeviceOps {
    Status (*submit)(Device*, Request*);
    Status (*cancel)(Device*, Request*);
    Status (*flush)(Device*);
    Status (*reset)(Device*);
    Status (*eject)(Device*);
};

enum class State : uint8_t { Discovered, Probing, Ready, Quiescing, Removed };

struct Device {
    uint32_t id;
    uint32_t parent_id;
    uint32_t generation;
    DeviceType type;
    Protocol protocol;
    Geometry geometry;
    uint32_t flags;
    State state;
    char name[32];
    char model[64];
    const DeviceOps* ops;
    void* driver_data;
};

struct DriverMatch {
    Protocol protocol;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    const char* compatible;
};

struct Driver {
    const char* name;
    const DriverMatch* matches;
    size_t match_count;
    Status (*probe)(Device* device);
    Status (*start)(Device* device);
    Status (*stop)(Device* device);
    Status (*reset)(Device* device);
    Status (*remove)(Device* device);
};

class Manager {
public:
    static void init();
    static Status register_driver(const Driver* driver);
    static const Driver* find_driver(Protocol protocol);
    static Status register_device(Device* device);
    static Status unregister_device(Device* device);
    static Device* find(uint32_t id);
    static Device* find_by_name(const char* name);
    static Status submit(Device* device, Request* request);
    static Status submit_sync(Device* device, Request* request);
    static Status set_state(Device* device, State state);
    static bool self_test();
};

namespace memory_block {
Device* device();
Status init();
}

const char* status_name(Status status);
const char* protocol_name(Protocol protocol);

} // namespace storage

#endif // OMEGA_KERNEL_STORAGE_HPP
