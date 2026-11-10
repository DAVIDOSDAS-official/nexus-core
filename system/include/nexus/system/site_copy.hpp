#pragma once

// Copying a website folder into what a public web server serves, as
// root (0.1.82). Two outside reviews shaped it:
//
//  - 0.1.76: a symlink in the folder was followed, so a link to
//    /etc/shadow became a public file. Then links leading out were
//    refused -- but checked in one pass and copied in another, so a
//    link swapped in between was still followed (0.1.82 review).
//  - 0.1.82: .git/ and .env were published with everything else.
//
// Now one pass, and nothing is ever followed: every name is opened
// relative to its folder's own descriptor with O_NOFOLLOW, a link is
// copied as a link (and only when it stays inside), and a hidden file
// or folder is left out (.well-known/ excepted -- it is for the web).
// Under sudo, only what the person who ran sudo could read is copied:
// root reading the folder must not publish what they could not.

#include <string>
#include <sys/types.h>
#include <vector>

namespace nexus::system {

struct SiteCopyResult {
    bool ok = false;
    std::string error;                 // why it stopped, when !ok
    std::size_t files = 0;
    std::vector<std::string> hidden;   // left out: hidden files and folders
    std::vector<std::string> unreadable;  // left out: not readable by `reader`
};

// The person whose read rights count; uid -1 = no check (not under sudo).
struct Reader {
    uid_t uid = static_cast<uid_t>(-1);
    std::vector<gid_t> groups;
};

// Copies the contents of `source` into `destination` (which must exist
// and be empty). Refuses the whole copy for a link that leads outside
// `source`, or a device, pipe or socket.
SiteCopyResult copySiteTree(const std::string& source, const std::string& destination,
                            const Reader& reader = {});

// Whether `reader` may read a file (or enter a folder) with this owner,
// group and mode. Root-free: mode bits only, no ACLs.
bool readerMay(const Reader& reader, uid_t owner, gid_t group, mode_t mode, bool directory);

}  // namespace nexus::system
