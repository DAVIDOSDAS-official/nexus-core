Profile: server
Description: Headless machine: remote access, firewall, time sync, logs
Requires: openssh-server,
 ufw | nftables | iptables,
 systemd-timesyncd | chrony | ntp,
 rsyslog | systemd-journal-remote,
 ca-certificates
