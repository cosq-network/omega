#include "kernel/storage.hpp"
#include "std/cstdint.hpp"

namespace storage::memory_block {
namespace {
static constexpr uint32_t BLOCK_SIZE = 512;
static constexpr uint64_t BLOCK_COUNT = 128;
static uint8_t backing[BLOCK_SIZE * BLOCK_COUNT] __attribute__((aligned(BLOCK_SIZE)));

static Status submit(Device* device, Request* request) {
    if (!device || !request) return Status::InvalidRequest;
    if (request->type == RequestType::Flush || request->type == RequestType::Identify) {
        if (request->complete) request->complete(request, Status::Success, request->context);
        return Status::Success;
    }
    if (request->type == RequestType::Eject) return Status::Unsupported;
    const size_t offset = static_cast<size_t>(request->lba) * BLOCK_SIZE;
    const size_t length = static_cast<size_t>(request->block_count) * BLOCK_SIZE;
    uint8_t* destination = reinterpret_cast<uint8_t*>(request->buffer);
    if (request->type == RequestType::Read) {
        for (size_t i = 0; i < length; ++i) destination[i] = backing[offset + i];
    } else if (request->type == RequestType::Write) {
        for (size_t i = 0; i < length; ++i) backing[offset + i] = destination[i];
    } else {
        return Status::Unsupported;
    }
    if (request->complete) request->complete(request, Status::Success, request->context);
    return Status::Success;
}

static Status cancel(Device*, Request*) { return Status::Unsupported; }
static Status flush(Device*) { return Status::Success; }
static Status reset(Device*) { return Status::Success; }
static Status eject(Device*) { return Status::Unsupported; }

static const DeviceOps ops{submit, cancel, flush, reset, eject};
static Device block_device{
    0, 0, 1, DeviceType::Virtual, Protocol::Memory,
    {BLOCK_COUNT, BLOCK_SIZE, BLOCK_SIZE, 32},
    DEVICE_FLUSH | DEVICE_WRITABLE | DEVICE_BARRIER, State::Discovered, "memtest0", "Omega memory block device",
    &ops, nullptr
};
}

Device* device() { return &block_device; }

Status init() {
    return Manager::register_device(&block_device);
}

} // namespace storage::memory_block
