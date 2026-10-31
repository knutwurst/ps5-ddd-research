# Disclaimer

## Purpose

This repository contains reverse-engineering notes and payload source code
documenting the Detachable Disc Drive (DDD) pairing subsystem on a specific
PlayStation 5 firmware (11.60). The investigation was motivated by a single
real recovery scenario: a legitimately owned PS5 Slim whose disc drive ended
up in the `DELETE_INCOMPLETE` state after a factory reset performed without
an internet connection.

The sole intent is interoperability research and personal device recovery.

## What this repository is NOT

- It is **not** a piracy tool.
- It does **not** circumvent AACS, BD+, or any other copy-protection scheme
  used on commercial Blu-ray discs or game media.
- It does **not** enable playback of unauthorized or backup copies of
  games or movies.
- It does **not** facilitate the use of modified or counterfeit disc media.
- It does **not** redistribute any proprietary binary code, firmware image,
  cryptographic key, or signed asset owned by Sony Interactive Entertainment
  or any third party.
- It does **not** publish any personally identifying information from the
  console under investigation (serial numbers, machine identifiers, and
  cryptographic key material visible in NVS dumps have been redacted).

The materials here are only useful on a system that has already been placed
into a non-production state by the owner (jailbroken firmware, ELF loader
running) and the content focuses on returning a drive the owner already paired
back to a working state.

## Legal framing

In the European Union and Germany specifically, reverse engineering of
software for interoperability is explicitly permitted under:

- **Directive 2009/24/EC**, Article 6
- **Urheberrechtsgesetz** (UrhG), §69e

Documentation of findings from legitimate reverse engineering is covered by
freedom of research and expression. The author of this repository is operating
within those legal frameworks.

If you are reading this from a jurisdiction with different rules
(for example the United States, where DMCA §1201 applies), you are
responsible for determining whether access or use is permitted locally.

## No warranty

Everything here is provided "as is", under the terms of the
[MIT License](LICENSE). Running any of the payloads on your own console
carries a non-zero risk of making a non-functional drive subsystem worse. The
author takes no responsibility for consequences of running this code against
any device, your own or otherwise.

## Prior art

This research builds on and complements earlier public work on the same
subsystem, in particular:

- `earthonion/ddd_pair_dump` — PS5 DDD 4-stage pairing pipeline dump tool
- `bc160201983/Troubleshoot-PS5-disc-drive-pairing` — DDD protocol analysis
- `ps5-payload-dev/sdk` — the ps5-payload-sdk toolchain used to build the
  payloads in this repository

## Takedown requests

If a rights holder believes any part of this repository oversteps the bounds
of legitimate interoperability research, please open an issue describing the
specific concern. Nothing here is published with intent to harm.
