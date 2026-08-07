#include "kernel/storage.hpp"
#include "kernel/dma.hpp"
#include "kernel/kprint.hpp"

namespace storage {
namespace {
static constexpr uint32_t MAX_DEVICES = 32;
static constexpr uint32_t MAX_DRIVERS = 16;
static Device* devices[MAX_DEVICES]{};
static const Driver* drivers[MAX_DRIVERS]{};
static uint32_t next_id = 1;
static bool initialized = false;

static size_t text_length(const char* value) {
    if (!value) return 0;
    size_t length = 0;
    while (value[length]) ++length;
    return length;
}

static bool text_equal(const char* left, const char* right) {
    if (!left || !right) return false;
    const size_t left_length = text_length(left);
    const size_t right_length = text_length(right);
    if (left_length != right_length) return false;
    for (size_t i = 0; i < left_length; ++i) if (left[i] != right[i]) return false;
    return true;
}

static Status validate_request(Device* device, Request* request) {
    if (!device || !request) return Status::InvalidRequest;
    if (device->state == State::Removed) return Status::DeviceRemoved;
    if (device->state != State::Ready) return Status::NotReady;
    if (request->type == RequestType::Flush || request->type == RequestType::Identify ||
        request->type == RequestType::Eject) return Status::Success;
    if (!request->buffer || request->block_count == 0) return Status::InvalidRequest;
    const uint32_t block_size = device->geometry.logical_block_size;
    if (block_size == 0 || device->geometry.max_transfer_blocks == 0) return Status::InvalidRequest;
    if (request->block_count > device->geometry.max_transfer_blocks) return Status::InvalidRequest;
    if (request->lba >= device->geometry.total_blocks ||
        request->block_count > device->geometry.total_blocks - request->lba) return Status::OutOfBounds;
    if (reinterpret_cast<uintptr_t>(request->buffer) % block_size) return Status::InvalidRequest;
    return Status::Success;
}

struct SyncContext {
    bool completed;
    Status status;
};

static void sync_complete(Request*, Status status, void* context) {
    SyncContext* sync = reinterpret_cast<SyncContext*>(context);
    if (sync) { sync->completed = true; sync->status = status; }
}
}

void Manager::init() {
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) devices[i] = nullptr;
    for (uint32_t i = 0; i < MAX_DRIVERS; ++i) drivers[i] = nullptr;
    next_id = 1;
    initialized = true;
    dma::init();
    kernel::kprintf("[+] Storage core initialized.\n");
}

Status Manager::register_driver(const Driver* driver) {
    if (!initialized || !driver || !driver->name || !driver->probe || !driver->start) return Status::InvalidRequest;
    for (uint32_t i = 0; i < MAX_DRIVERS; ++i) if (drivers[i] == driver) return Status::InvalidRequest;
    for (uint32_t i = 0; i < MAX_DRIVERS; ++i) {
        if (!drivers[i]) { drivers[i] = driver; return Status::Success; }
    }
    return Status::IoError;
}

const Driver* Manager::find_driver(Protocol protocol) {
    for (uint32_t i = 0; i < MAX_DRIVERS; ++i) {
        if (!drivers[i] || !drivers[i]->matches) continue;
        for (size_t m = 0; m < drivers[i]->match_count; ++m) {
            if (drivers[i]->matches[m].protocol == protocol) return drivers[i];
        }
    }
    return nullptr;
}

Status Manager::register_device(Device* device) {
    if (!initialized || !device || !device->ops || !device->ops->submit) return Status::InvalidRequest;
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) {
        if (devices[i] == device) return Status::InvalidRequest;
    }
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) {
        if (!devices[i]) {
            if (device->id == 0) device->id = next_id++;
            if (device->generation == 0) device->generation = 1;
            if (device->state == State::Discovered) device->state = State::Ready;
            devices[i] = device;
            return Status::Success;
        }
    }
    return Status::IoError;
}

Status Manager::unregister_device(Device* device) {
    if (!device) return Status::InvalidRequest;
    device->state = State::Quiescing;
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) {
        if (devices[i] == device) {
            devices[i] = nullptr;
            device->state = State::Removed;
            ++device->generation;
            return Status::Success;
        }
    }
    return Status::InvalidRequest;
}

Device* Manager::find(uint32_t id) {
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) if (devices[i] && devices[i]->id == id) return devices[i];
    return nullptr;
}

Device* Manager::find_by_name(const char* name) {
    for (uint32_t i = 0; i < MAX_DEVICES; ++i) if (devices[i] && text_equal(devices[i]->name, name)) return devices[i];
    return nullptr;
}

Status Manager::submit(Device* device, Request* request) {
    const Status validation = validate_request(device, request);
    if (validation != Status::Success) return validation;
    if (request->type == RequestType::Write) {
        if (device->flags & DEVICE_READ_ONLY || !(device->flags & DEVICE_WRITABLE)) return Status::ReadOnly;
        if ((request->flags & (REQUEST_FUA | REQUEST_BARRIER)) && !(device->flags & DEVICE_FLUSH)) return Status::Unsupported;
    }
    if (request->type == RequestType::Flush) {
        if (!(device->flags & DEVICE_FLUSH) || !device->ops->flush) return Status::Unsupported;
        return device->ops->flush(device);
    }
    const Status result = device->ops->submit(device, request);
    if (result != Status::Success || request->type != RequestType::Write ||
        !(request->flags & (REQUEST_FUA | REQUEST_BARRIER))) return result;
    return device->ops->flush ? device->ops->flush(device) : Status::Unsupported;
}

Status Manager::submit_sync(Device* device, Request* request) {
    if (!request) return Status::InvalidRequest;
    SyncContext sync{false, Status::IoError};
    const Completion original = request->complete;
    void* original_context = request->context;
    request->complete = sync_complete;
    request->context = &sync;
    const Status submit_status = submit(device, request);
    if (submit_status != Status::Success) {
        request->complete = original;
        request->context = original_context;
        return submit_status;
    }
    // Flush is exposed as a synchronous DeviceOps operation rather than as a
    // callback-bearing request. A successful flush therefore completes the
    // synchronous wrapper even though no request callback was invoked.
    if (request->type == RequestType::Flush && !sync.completed) {
        request->complete = original;
        request->context = original_context;
        return Status::Success;
    }
    if (!sync.completed) {
        request->complete = original;
        request->context = original_context;
        return Status::Timeout;
    }
    const Status result = sync.status;
    request->complete = original;
    request->context = original_context;
    return result;
}

Status Manager::set_state(Device* device, State state) {
    if (!device) return Status::InvalidRequest;
    if (state == State::Removed) return unregister_device(device);
    device->state = state;
    return Status::Success;
}

bool Manager::self_test() {
    Device* device = memory_block::device();
    if (!device) return false;
    uint8_t write_buffer[512] __attribute__((aligned(512))){};
    uint8_t read_buffer[512] __attribute__((aligned(512))){};
    for (uint32_t i = 0; i < 512; ++i) write_buffer[i] = static_cast<uint8_t>(i ^ 0x5A);
    Request write{RequestType::Write, 2, 1, write_buffer, 0, nullptr, nullptr};
    Request read{RequestType::Read, 2, 1, read_buffer, 0, nullptr, nullptr};
    if (submit_sync(device, &write) != Status::Success) return false;
    if (submit_sync(device, &read) != Status::Success) return false;
    for (uint32_t i = 0; i < 512; ++i) if (read_buffer[i] != write_buffer[i]) return false;
    Request fua_write{RequestType::Write, 3, 1, write_buffer, REQUEST_FUA, nullptr, nullptr};
    if (submit_sync(device, &fua_write) != Status::Success) return false;
    Request outside{RequestType::Read, device->geometry.total_blocks, 1, read_buffer, 0, nullptr, nullptr};
    if (submit(device, &outside) != Status::OutOfBounds) return false;
    return true;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Success: return "success";
        case Status::InvalidRequest: return "invalid-request";
        case Status::OutOfBounds: return "out-of-bounds";
        case Status::NotReady: return "not-ready";
        case Status::Timeout: return "timeout";
        case Status::DeviceRemoved: return "device-removed";
        case Status::MediaChanged: return "media-changed";
        case Status::Unsupported: return "unsupported";
        case Status::ProtocolError: return "protocol-error";
        case Status::ResetRequired: return "reset-required";
        case Status::DmaFailure: return "dma-failure";
        case Status::ReadOnly: return "read-only";
        case Status::IoError: return "io-error";
    }
    return "unknown";
}

const char* protocol_name(Protocol protocol) {
    switch (protocol) {
        case Protocol::Memory: return "memory";
        case Protocol::VirtioBlock: return "virtio-block";
        case Protocol::Nvme: return "nvme";
        case Protocol::AhciAta: return "ahci-ata";
        case Protocol::Atapi: return "atapi";
        case Protocol::Sdhci: return "sdhci";
        case Protocol::UsbMassStorage: return "usb-mass-storage";
    }
    return "unknown";
}

} // namespace storage
