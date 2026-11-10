# Security policy

## Supported versions

Fixes go into the next release only. Please update to the latest release before reporting; an issue that only exists in an
older release is fixed by updating.

| Version | Fixes |
|---|---|
| latest release | yes |
| anything older | no, update first |

## Reporting a vulnerability

Please report it privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**.
Don't open a public issue, discussion or pull request for it, and don't post details or a proof of concept in public
before a fix is released. If the Security tab offers no way to report, open an issue that only asks for a private channel,
without any details.

Please include the host version (the app's **Diagnostics** page or `host_diag.log`), the firmware, whether the debug build
was used, which port or feature is involved, and the steps that show the problem. You get an answer as soon as the
maintainer can look at it; this is a spare-time project without a fixed response time.

## Scope

In scope, for the host payload and the app in this repository:

- Everything the host answers on the network: the Moonlight ports (TCP 47984, 47989, 48010; UDP 47998, 47999, 48000 and
  the same ports moved by **Base port**), the web page (TCP 47990), mDNS (UDP 5353), the report port (TCP 47996), and the
  port forwarding by UPnP.
- Pairing, and the limits for paired devices: profiles per device, hidden apps, access to the console's menus, ending
  another device's stream.
- The control socket (TCP 47995) as far as it is reachable from somewhere other than the console itself, and the file
  bridge behind it.
- Secrets the host keeps in `/data/prosperosunshine/` (the pairing key, the web page's login).
- Patches this project applies to Sunshine and its dependencies (`patches/`).

Out of scope:

- The jailbreak, the ELF loader (TCP 9021) and other payloads: anything that already runs code on the console, or can
  send it to the ELF loader, controls the console anyway. See [docs/security.md](docs/security.md) (German) for the
  threat model.
- Bugs in upstream Sunshine, Moonlight or a dependency that this project doesn't change: please report them upstream.
  If they reach the PS5 host in a way upstream can't fix, they are in scope here.
- Attacks that need physical access to the console or the owner's own devices.
- Denial of service that needs more bandwidth than the console's connection carries.
