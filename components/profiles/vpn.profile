Profile: vpn
Description: WireGuard and OpenVPN, connected from the network menu
# WireGuard first: fewer moving parts, a config a person can read, and
# it is in the kernel. OpenVPN because that is what most VPN
# providers and workplaces still hand out, as .ovpn files; with the
# desktop's plugin those import straight into the network settings,
# no terminal needed.
Requires: vpn-client,
 openvpn-client
Prefers: vpn-client=wireguard-tools
