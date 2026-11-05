Profile: cybersecurity
Description: Network, web and password tools here; every Kali tool on request in a container (nexus cyber)
# Named by what the tools do, not by which distribution collected
# them. Kali, Parrot and BlackArch are curated sets rather than
# unique software: most of what they ship exists in ordinary
# repositories, and what does not is reachable through a container.
#
# The everyday tools below are installed into the system. The rest --
# Kali's ten most used, its default set, or everything it packages --
# goes into a Distrobox container with `nexus cyber create`, so it is
# all there for whoever wants it and costs nothing to anyone else.
# (Was "security" until 0.1.67; that name still works.)
#
# Adding Kali's repository to a Debian or Ubuntu system is not one of
# the options. Kali rolling tracks Debian testing; an Ubuntu release
# does not, and the two disagree about libc. That is the mixing
# problem, and it breaks systems rather than extending them.
Requires: network-scanner,
 packet-analyser,
 password-cracker,
 web-scanner,
 network-auditor,
 reverse-engineering,
 distrobox
