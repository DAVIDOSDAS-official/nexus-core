Profile: minimal
Description: The least that still works, for machines with little to spare
# The least that still works, plus anything else, is not the least
# that works. Combining this is a different request, not a smaller
# one, so it is refused with a pointer to the profile that does
# combine.
Exclusive: yes
Instead-Use: minimalism
Requires: init,
 c-library,
 core-utilities,
 package-manager,
 lightweight-session,
 terminal-emulator,
 text-editor,
 file-manager
# Stated for the same reason minimalism states its own: without a
# preference, generating an image for a bare machine picks whatever is
# cheapest per capability, and a profile called minimal would end up
# with a full desktop's file manager because nothing said otherwise.
#
# On a machine that already has tools these are only tie-breakers, so
# an existing setup still resolves to its own.
Prefers: lightweight-session=openbox,
 terminal-emulator=foot,
 text-editor=nano,
 file-manager=pcmanfm
