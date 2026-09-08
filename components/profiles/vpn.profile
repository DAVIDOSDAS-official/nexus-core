Profile: vpn
Description: A WireGuard client, and the tools to configure it
# WireGuard rather than OpenVPN by default: fewer moving parts, a
# config a person can read, and it is in the kernel.
Requires: vpn-client
Prefers: vpn-client=wireguard-tools
