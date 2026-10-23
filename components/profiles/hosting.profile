Profile: hosting
Description: Run servers here: Minecraft, websites, and Cockpit to manage it from a browser
# What `nexus server` needs (19-23 October). The servers themselves are
# containers that `nexus server add` downloads when asked, not packages:
# one Minecraft or website more or less should not need a restart, and
# a container is removed without a trace.
#
# Podman is usually in the image already; listed so a machine without
# it gets it. Cockpit is the browser page (https://<address>:9090), and
# cockpit-podman its page for the containers. The firewall is whatever
# the machine has; `nexus server` opens only the ports its servers use
# when firewalld is running, and says so when nothing is.
#
# No SSH server: Cockpit covers managing it from another device, and an
# SSH server open to the network is a decision of its own.
Requires: container-runtime,
 web-admin,
 container-admin,
 firewall
