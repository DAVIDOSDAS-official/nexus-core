Profile: base
Description: What any usable system needs regardless of what it is for
# network-manager is in base rather than in a desktop profile.
#
# minimal shipped without it, and the result was a laptop that could
# not reach a repository -- so the first-boot picker offered twelve
# profiles and could install none of them. A machine that cannot get
# online cannot be set up, whatever else it has.
Requires: init,
 network-manager,
 network-manager-wifi,
 wifi-supplicant,
 c-library,
 core-utilities,
 package-manager,
 ca-certificates,
 archiver
