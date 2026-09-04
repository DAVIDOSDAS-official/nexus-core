Profile: security
Description: Network, web and password auditing tools
# Named by what the tools do, not by which distribution collected
# them. Kali, Parrot and BlackArch are curated sets rather than
# unique software: most of what they ship exists in ordinary
# repositories, and what does not is reachable through a container.
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
 reverse-engineering
