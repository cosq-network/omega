#include "kernel/security.hpp"
#include "kernel/vfs.hpp"

extern "C" void* kmalloc(size_t size) {
    static uint8_t heap[4096];
    static size_t used = 0;
    if (used + size > sizeof(heap)) return nullptr;
    void* result = heap + used;
    used += (size + 7) & ~static_cast<size_t>(7);
    return result;
}
namespace kernel { void kprintf(const char*, ...) {} }

static int read_file(vfs::VfsNode*, size_t, size_t size, uint8_t* buffer) {
    if (buffer && size) buffer[0] = 'O';
    return static_cast<int>(size);
}
static int write_file(vfs::VfsNode*, size_t, size_t size, const uint8_t*) {
    return static_cast<int>(size);
}

int main() {
    security::Manager::init();
    security::gid_t groups[] = {100};
    if (security::Manager::setgid(100) != 0) return 1;
    if (security::Manager::setgroups(groups, 1) != 0) return 2;
    if (security::Manager::setuid(1000) != 0) return 3;

    vfs::VfsNode node{};
    node.uid = 2000;
    node.gid = 100;
    node.mode = 0640;
    node.read = read_file;
    node.write = write_file;
    uint8_t buffer[1]{};
    if (vfs::VirtualFilesystem::read(&node, 0, 1, buffer) != 1 || buffer[0] != 'O') return 4;
    if (vfs::VirtualFilesystem::write(&node, 0, 1, buffer) != -1) return 5;
    if (security::Manager::self_test() != 0) return 6;
    return 0;
}
