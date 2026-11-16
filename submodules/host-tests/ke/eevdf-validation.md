# EEVDF validation

The `/EEVDF` loader option selects the experimental policy. Without it, the
existing NT policy is used. The scheduler keeps strict NT priority levels and
leaves real-time priorities under NT scheduling. Ordinary foreground-process
threads receive a minimum EEVDF weight of 3072; a six-unit background quantum
has weight 1024. This is an experimental policy, not a Windows or Linux parity
claim.

On 2026-11-16, the isolated worktree's AMD64 debug kernel was booted in two
two-CPU KVM snapshots with the same guest image, kernel, and probe executables.
One boot used `/EEVDF`; the other used the default NT policy. The test programs
were installed on stopped disposable overlays with Linux guest-image tools.
Both boots reached the desktop. The EEVDF boot log reported that the policy
was enabled and that virtual-deadline selection was active.

| Guest probe | NT | EEVDF |
| --- | --- | --- |
| Same-priority worker counts | 3,751,456,768 / 3,577,176,064 | 3,018,706,944 / 2,924,646,400 |
| Higher / lower-priority worker counts | 1,279,569,920 / 4,096 | 942,714,880 / 4,096 |
| Affinity migrations and priority changes | 100 / 100 | 100 / 100 |
| Maximum observed wakeup wait | 0 ms | 31 ms |
| Foreground / background counts, both at priority 8 | 3,842,686,976 / 3,984,515,072 | 4,741,140,480 / 1,682,505,728 |

Both probes reported `PASS` in both boots. The counts show the intended
foreground weighting in this particular workload; they are single-run values,
not a throughput, latency, or responsiveness benchmark. Tick resolution and
guest load affect the wakeup measurements.

A separate four-CPU EEVDF snapshot also passed both probes. Its pinned
foreground/background counts were 5,739,065,344 / 2,011,758,592; its general
probe completed 100 affinity migrations and 100 priority changes. The probe's
migration loop uses two of the available processors.

An i386 debug boot image was built and booted with two KVM virtual CPUs and
`/EEVDF`. Its serial log reported that the policy was enabled, that
virtual-deadline selection was active, and that Program Manager registered
for device notifications. This was a boot smoke test; the guest probes were
not run in that image.

The host suite can be run with:

```sh
ctest --test-dir output-host-tests -R '^ke.eevdf_' --output-on-failure
```

It exercises placement, bounded and decaying sleep lag, priority-specific
debt, reweighting, deadline carry-over, migration, accounting, queue insertion,
clock reviews, foreground transitions, and the switch handoff. AMD64, i386,
and ARM64 debug kernel targets have linked successfully. An ARM64 guest boot,
a Windows 11 reference run, long-duration SMP stress, and latency and
scalability measurements remain outstanding. The implementation uses a global
state spinlock and linear runnable-set scans, so large-system contention needs
measurement before considering it production-ready.
