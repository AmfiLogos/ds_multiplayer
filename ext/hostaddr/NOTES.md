# hostaddr (owner 2026-10-18): the host's address in the in-game Players panel

- Host only (guests never see it), player list open: one line per usable local IPv4 address, "Address: <ip>:<port>"
  (port = mp_server_port, the TCP server bound to 0.0.0.0), each with a Copy button (BottomButton; "Copied" for 2 s),
  then the hint "Friends outside your network need your public IP and the port opened on the router." No public IP
  lookup. Addresses are read again every 5 s (src/ui_game.c host_addrs_read).
- Order and labels: src/ui_model.c ui_host_addrs (en0 first, other real adapters, then virtual = vmnet / bridge / vnic /
  vboxnet, then VPN = utun / ppp / tun / tap); loopback, 169.254 and repeats left out. Unit test: ui_test test_host_addrs.
- Windows: win/port/posix/ifaddrs.h names adapters by kind from their description (VMware / VirtualBox / Hyper-V =
  "vmnet<n>", TAP / tunnel / VPN = "utun<n>", real ones "en0".. in order); mpmenu's lobby "prefer en0" now skips
  virtual adapters on Windows too.
- Test command: ui_host_addr {copy: k}. Pair test: ext/hostaddr/hostaddr_pair.py --host-lang en|ja (one pair, hidden):
  address lines with the port, router hint, none on the guest, Copy = first address on the clipboard. 2026-10-18:
  en 4/4 PASS, ja 4/4 PASS.
