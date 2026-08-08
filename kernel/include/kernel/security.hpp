#ifndef OMEGA_KERNEL_SECURITY_HPP
#define OMEGA_KERNEL_SECURITY_HPP

#include "std/cstdint.hpp"

namespace security {

using uid_t = uint32_t;
using gid_t = uint32_t;

constexpr uid_t ROOT_UID = 0;
constexpr gid_t ROOT_GID = 0;
constexpr uint32_t MAX_GROUPS = 32;

enum Access {
    MAY_EXEC = 1,
    MAY_WRITE = 2,
    MAY_READ = 4,
};

struct Credentials {
    uid_t uid, euid, suid, fsuid;
    gid_t gid, egid, sgid, fsgid;
    gid_t groups[MAX_GROUPS];
    uint32_t group_count;
    uint32_t umask;
};

class Manager {
public:
    static void init();
    // Bind credential operations to the currently scheduled process.  The
    // fallback is retained for early boot and standalone unit tests.
    static void bind(Credentials* credentials);
    static const Credentials& current();
    static int setuid(uid_t uid);
    static int setgid(gid_t gid);
    static int setresuid(uid_t real, uid_t effective, uid_t saved);
    static int setresgid(gid_t real, gid_t effective, gid_t saved);
    static int setgroups(const gid_t* groups, uint32_t count);
    static uint32_t getgroups(gid_t* groups, uint32_t capacity);
    static int set_umask(uint32_t mask);
    static bool is_member(gid_t gid, const Credentials& credentials);
    static bool check(uint32_t mode, uid_t owner, gid_t group,
                      const Credentials& credentials, uint32_t access);
    static int self_test();
};

} // namespace security

#endif // OMEGA_KERNEL_SECURITY_HPP
