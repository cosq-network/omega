#include "kernel/security.hpp"

namespace security {

namespace {
Credentials credentials = {0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0022};
Credentials* active_credentials = nullptr;
}

void Manager::init() {
    credentials = {ROOT_UID, ROOT_UID, ROOT_UID, ROOT_UID,
                   ROOT_GID, ROOT_GID, ROOT_GID, ROOT_GID, {ROOT_GID}, 1, 0022};
    active_credentials = nullptr;
}

void Manager::bind(Credentials* value) { active_credentials = value; }

const Credentials& Manager::current() {
    return active_credentials != nullptr ? *active_credentials : credentials;
}

int Manager::setuid(uid_t uid) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    if (value.euid != ROOT_UID && uid != value.uid && uid != value.suid) return 1; // EPERM
    value.uid = uid;
    value.euid = uid;
    value.suid = uid;
    value.fsuid = uid;
    return 0;
}

int Manager::setgid(gid_t gid) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    if (value.euid != ROOT_UID && gid != value.gid && gid != value.sgid) return 1;
    value.gid = gid;
    value.egid = gid;
    value.sgid = gid;
    value.fsgid = gid;
    return 0;
}

int Manager::setresuid(uid_t real, uid_t effective, uid_t saved) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    if (value.euid != ROOT_UID &&
        ((real != static_cast<uid_t>(-1) && real != value.uid && real != value.suid) ||
         (effective != static_cast<uid_t>(-1) && effective != value.uid && effective != value.suid) ||
         (saved != static_cast<uid_t>(-1) && saved != value.uid && saved != value.suid))) return 1;
    if (real != static_cast<uid_t>(-1)) value.uid = real;
    if (effective != static_cast<uid_t>(-1)) value.euid = effective;
    if (saved != static_cast<uid_t>(-1)) value.suid = saved;
    value.fsuid = value.euid;
    return 0;
}

int Manager::setresgid(gid_t real, gid_t effective, gid_t saved) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    if (value.euid != ROOT_UID &&
        ((real != static_cast<gid_t>(-1) && real != value.gid && real != value.sgid) ||
         (effective != static_cast<gid_t>(-1) && effective != value.gid && effective != value.sgid) ||
         (saved != static_cast<gid_t>(-1) && saved != value.gid && saved != value.sgid))) return 1;
    if (real != static_cast<gid_t>(-1)) value.gid = real;
    if (effective != static_cast<gid_t>(-1)) value.egid = effective;
    if (saved != static_cast<gid_t>(-1)) value.sgid = saved;
    value.fsgid = value.egid;
    return 0;
}

int Manager::setgroups(const gid_t* groups, uint32_t count) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    if (value.euid != ROOT_UID) return 1;
    if (count > MAX_GROUPS || (count != 0 && groups == nullptr)) return 22;
    for (uint32_t i = 0; i < count; ++i) value.groups[i] = groups[i];
    value.group_count = count;
    return 0;
}

uint32_t Manager::getgroups(gid_t* groups, uint32_t capacity) {
    const Credentials& value = current();
    if (groups == nullptr && capacity != 0) return static_cast<uint32_t>(-1);
    if (capacity < value.group_count) return static_cast<uint32_t>(-1);
    for (uint32_t i = 0; i < value.group_count; ++i) groups[i] = value.groups[i];
    return value.group_count;
}

int Manager::set_umask(uint32_t mask) {
    Credentials& value = active_credentials != nullptr ? *active_credentials : credentials;
    const uint32_t old = value.umask;
    value.umask = mask & 0777;
    return static_cast<int>(old);
}

bool Manager::is_member(gid_t gid, const Credentials& value) {
    if (value.fsgid == gid) return true;
    for (uint32_t i = 0; i < value.group_count; ++i) if (value.groups[i] == gid) return true;
    return false;
}

bool Manager::check(uint32_t mode, uid_t owner, gid_t group,
                    const Credentials& value, uint32_t access) {
    uint32_t bits = mode & 07;
    if (value.fsuid == owner) bits = (mode >> 6) & 07;
    else if (is_member(group, value)) bits = (mode >> 3) & 07;
    if (value.euid == ROOT_UID) {
        if ((access & MAY_EXEC) && !(mode & 0111)) return false;
        return true;
    }
    if ((access & MAY_READ) && !(bits & 4)) return false;
    if ((access & MAY_WRITE) && !(bits & 2)) return false;
    if ((access & MAY_EXEC) && !(bits & 1)) return false;
    return true;
}

int Manager::self_test() {
    Credentials user = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                        {1000, 100}, 2, 0022};
    if (!check(0644, 1000, 100, user, MAY_READ | MAY_WRITE)) return 22;
    if (check(0640, 2000, 300, user, MAY_READ)) return 22;
    if (!check(0640, 2000, 100, user, MAY_READ)) return 22;
    Credentials root = user; root.euid = ROOT_UID;
    if (!check(0000, 2000, 300, root, MAY_READ)) return 22;
    return 0;
}

} // namespace security
