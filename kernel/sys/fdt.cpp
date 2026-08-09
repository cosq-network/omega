#include "kernel/fdt.hpp"

namespace {
alignas(8) static uintptr_t g_boot_fdt = 0;

static uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
static uint64_t cells(const uint8_t* p, uint32_t count) {
    uint64_t v = 0; for (uint32_t i = 0; i < count; ++i) v = (v << 32) | be32(p + i * 4); return v;
}
static size_t length(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
static bool equal(const char* a, const char* b, size_t limit = 256) {
    for (size_t i = 0; i < limit; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}
static bool has_compatible(const char* s, uint32_t n, const char* wanted) {
    const uint32_t wanted_len = static_cast<uint32_t>(length(wanted));
    for (uint32_t pos = 0; pos < n;) {
        uint32_t end = pos; while (end < n && s[end]) ++end;
        if (end - pos == wanted_len) { bool ok = true; for (uint32_t i = 0; i < wanted_len; ++i) if (s[pos+i] != wanted[i]) ok = false; if (ok) return true; }
        pos = end + 1;
    }
    return false;
}
static void format(fdt::SimpleFramebuffer* fb, const char* s, uint32_t n) {
    fb->bpp = 32; fb->red_mask = fb->green_mask = fb->blue_mask = 0xFF;
    fb->red_shift = 16; fb->green_shift = 8; fb->blue_shift = 0; fb->alpha = false;
    if (equal(s, "r5g6b5", n)) { fb->bpp=16; fb->red_mask=0x1F; fb->green_mask=0x3F; fb->blue_mask=0x1F; fb->red_shift=11; fb->green_shift=5; }
    else if (equal(s, "x1r5g5b5", n)) { fb->bpp=16; fb->red_mask=fb->green_mask=fb->blue_mask=0x1F; fb->red_shift=10; fb->green_shift=5; }
    else if (equal(s, "a8r8g8b8", n)) fb->alpha = true;
}
}

namespace fdt {
uintptr_t boot_pointer() { return g_boot_fdt; }
void set_boot_pointer(uintptr_t pointer) { g_boot_fdt = pointer; }
bool is_valid_blob(uintptr_t pointer) {
    if (pointer == 0 || (pointer & 3u)) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(pointer);
    if (be32(base) != 0xD00DFEEDu) return false;
    const uint32_t total = be32(base + 4);
    const uint32_t structure = be32(base + 8);
    const uint32_t strings = be32(base + 12);
    const uint32_t structure_size = be32(base + 36);
    const uint32_t strings_size = be32(base + 32);
    return total >= 40 && total <= 16u * 1024u * 1024u && structure < total &&
           strings < total && structure_size <= total - structure &&
           strings_size <= total - strings;
}

bool find_simple_framebuffer(SimpleFramebuffer* out) {
    if (!out || !is_valid_blob(g_boot_fdt)) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(g_boot_fdt);
    if (be32(base) != 0xD00DFEEDu) return false;
    const uint32_t total = be32(base+4), struct_off=be32(base+8), strings_off=be32(base+12), struct_size=be32(base+36), strings_size=be32(base+32);
    if (total < 40 || total > 16u*1024u*1024u || struct_off >= total || strings_off >= total || struct_size > total-struct_off || strings_size > total-strings_off) return false;
    const uint8_t* structure = base + struct_off; const char* strings = reinterpret_cast<const char*>(base + strings_off);
    struct Node { bool candidate, reg, width, height, stride, fmt; SimpleFramebuffer fb; };
    Node stack[16]{}; uint32_t depth=0, address_cells=2, size_cells=1, off=0;
    uint32_t tokens = 0;
    while (off + 4 <= struct_size && tokens++ < 65536) {
        const uint32_t token=be32(structure+off); off+=4;
        if (token==1) {
            if (depth>=16) return false; Node node{}; const char* name=reinterpret_cast<const char*>(structure+off); size_t n=0;
            while (off+n<struct_size && name[n]) ++n; if (off+n>=struct_size) return false; off+=(n+4)&~3u;
            node.candidate=equal(name,"simple-framebuffer", n + 1) || equal(name,"framebuffer", n + 1); stack[depth++]=node;
        } else if (token==2) {
            if (!depth) return false; const Node& node=stack[depth-1];
            if (node.candidate && node.reg && node.width && node.height && node.stride && node.fmt && node.fb.phys_addr && node.fb.width && node.fb.height) { *out=node.fb; return true; }
            --depth;
        } else if (token==3) {
            if (off+8>struct_size || !depth) return false; const uint32_t n=be32(structure+off), nameoff=be32(structure+off+4); off+=8;
            if (off+((n+3)&~3u)>struct_size || nameoff>=strings_size) return false; Node& node=stack[depth-1]; const char* prop=strings+nameoff; const uint8_t* value=structure+off;
            const size_t prop_limit = strings_size - nameoff;
            if (equal(prop,"#address-cells", prop_limit) && n>=4 && depth==1) { address_cells=be32(value); if (address_cells > 2) return false; }
            else if (equal(prop,"#size-cells", prop_limit) && n>=4 && depth==1) { size_cells=be32(value); if (size_cells > 2) return false; }
            else if (node.candidate && equal(prop,"compatible", prop_limit)) node.candidate=has_compatible(reinterpret_cast<const char*>(value),n,"simple-framebuffer");
            else if (node.candidate && equal(prop,"reg", prop_limit) && n >= (address_cells+size_cells)*4) { node.fb.phys_addr=static_cast<uintptr_t>(cells(value,address_cells)); node.fb.size=cells(value+address_cells*4,size_cells); node.reg=true; }
            else if (node.candidate && equal(prop,"width", prop_limit) && n>=4) { node.fb.width=be32(value); node.width=true; }
            else if (node.candidate && equal(prop,"height", prop_limit) && n>=4) { node.fb.height=be32(value); node.height=true; }
            else if (node.candidate && equal(prop,"stride", prop_limit) && n>=4) { node.fb.stride=be32(value); node.stride=true; }
            else if (node.candidate && equal(prop,"format", prop_limit) && n) { format(&node.fb,reinterpret_cast<const char*>(value),n); node.fmt=true; }
            off+=(n+3)&~3u;
        } else if (token==4) continue;
        else if (token==9) break;
        else return false;
    }
    return false;
}

bool find_virtio_mmio(VirtioMmioDevice* out, uint32_t ordinal) {
    if (!out || !is_valid_blob(g_boot_fdt)) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(g_boot_fdt);
    if (be32(base) != 0xD00DFEEDu) return false;
    const uint32_t total = be32(base+4), struct_off=be32(base+8), strings_off=be32(base+12), struct_size=be32(base+36), strings_size=be32(base+32);
    if (total < 40 || total > 16u*1024u*1024u || struct_off >= total || strings_off >= total || struct_size > total-struct_off || strings_size > total-strings_off) return false;
    const uint8_t* structure = base + struct_off; const char* strings = reinterpret_cast<const char*>(base + strings_off);
    struct Node { bool candidate, reg; VirtioMmioDevice device; };
    Node stack[16]{}; uint32_t depth=0, address_cells=2, size_cells=1, off=0, found=0;
    uint32_t tokens = 0;
    while (off + 4 <= struct_size && tokens++ < 65536) {
        const uint32_t token=be32(structure+off); off+=4;
        if (token==1) {
            if (depth>=16) return false; Node node{}; const char* name=reinterpret_cast<const char*>(structure+off); size_t n=0;
            while (off+n<struct_size && name[n]) ++n; if (off+n>=struct_size) return false; off+=(n+4)&~3u; stack[depth++]=node;
        } else if (token==2) {
            if (!depth) return false; const Node& node=stack[depth-1];
            if (node.candidate && node.reg) { if (found++ == ordinal) { *out=node.device; return true; } }
            --depth;
        } else if (token==3) {
            if (off+8>struct_size || !depth) return false; const uint32_t n=be32(structure+off), nameoff=be32(structure+off+4); off+=8;
            if (off+((n+3)&~3u)>struct_size || nameoff>=strings_size) return false; Node& node=stack[depth-1]; const char* prop=strings+nameoff; const uint8_t* value=structure+off;
            const size_t prop_limit = strings_size - nameoff;
            if (equal(prop,"#address-cells", prop_limit) && n>=4 && depth==1) { address_cells=be32(value); if (address_cells > 2) return false; }
            else if (equal(prop,"#size-cells", prop_limit) && n>=4 && depth==1) { size_cells=be32(value); if (size_cells > 2) return false; }
            else if (equal(prop,"compatible", prop_limit)) node.candidate=has_compatible(reinterpret_cast<const char*>(value),n,"virtio,mmio");
            else if (equal(prop,"reg", prop_limit) && n >= (address_cells+size_cells)*4) { node.device.phys_addr=static_cast<uintptr_t>(cells(value,address_cells)); node.device.size=cells(value+address_cells*4,size_cells); node.reg=true; }
            off+=(n+3)&~3u;
        } else if (token==4) continue;
        else if (token==9) break;
        else return false;
    }
    return false;
}
}

extern "C" void omega_set_fdt_pointer(uintptr_t pointer) { fdt::set_boot_pointer(pointer); }
