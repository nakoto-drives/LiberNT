# Experimental scheduler model

This is a single-CPU reference model for comparing NT-style round robin with an
EEVDF-inspired policy. Run it with `python -B eevdf_model_test.py` in this
directory, or run the `ke.eevdf_model` CTest test. It is not a kernel scheduler
and does not change boot behavior.

The kernel build also contains `ntos/ke/eevdf_math.c`, a separately tested
integer eligibility and earliest-deadline selector used by the domain
implementation described below. Its accumulation interface permits queue traversal without a
temporary candidate array; a caller must hold the appropriate locks and supply
a consistent snapshot across passes. Arithmetic overflow is reported to the
caller instead of silently changing eligibility or deadline order. Runtime is
carried as a fixed-point value with a remainder so that accounting in small
increments agrees with accounting for the same execution in one increment.

`ntos/ke/eevdf_dispatch.c` provides a ready-queue selector for one ordinary NT
priority. The caller supplies a metadata query and holds the PRCB lock and any
lock needed to keep the metadata unchanged through all three scans. It returns
failure without touching the queue or output if metadata is unavailable, a
thread is still leaving a processor, or arithmetic overflows. The caller can
then use the normal NT queue order. The ready-thread dispatcher now calls this
selector when the policy flag is enabled; `/EEVDF` selects it at boot on
architectures with the switch-handoff protocol. The default remains NT.
Private per-thread state is registered and
released with normal thread lifetime, and execution is charged at switch-out.
Ready, standby, and stealing paths place eligible thread state into domains on
architectures with the switch-handoff protocol. This selector still considers
the ready queue alone in general dispatcher calls. At quantum end, a separate
domain selection includes the running thread and verifies that all domain
members match the current processor's ready queue before choosing.
The clock requests an EEVDF review for a running ordinary-priority thread on
each tick, including when its NT quantum has not expired. This accounts for
domain progress while peers sleep. An EEVDF preemption preserves the remaining
NT quantum so normal quantum expiration and boost decay still take place.

`ntos/ke/eevdf_domain.c` implements a lock-external runnable domain, including
the currently running entity when the caller puts it in the domain. Its join,
leave and migration operations preserve bounded signed lag, adjust placement
for the destination's weight, and retain fractional runtime and the remaining
deadline of an unfinished request across moves. A
weight or slice change retains bounded lag, and a domain can be rebased without
changing relative deadlines or eligibility.
The switch-completion path charges the final runtime before saving lag when a
thread leaves its domain. Per-thread state embeds the domain entity and its
destructor removes it from any domain. The state layer has per-processor,
per-priority domains and a placement operation for changing processor, priority,
weight and slice. Each thread keeps bounded lag separately for each ordinary
priority, so a temporary priority boost does not erase debt at its old level.
Negative sleeping lag decays toward zero as the source priority domain accrues
service; time spent with no runnable peer does not itself forgive the debt.
These operations and the ready-queue insertion path are
tested on the host, but priority changes and all dispatcher transitions still
need end-to-end testing. The i386 switch path activates its synchronized
handoff only when EEVDF is selected, leaving the default handoff unchanged.

The domain selector requires the state lock, nested inside the owning PRCB
lock, throughout selection and use of the returned thread. A caller must
validate and update the dispatcher state while those locks protect it. Domain
transitions use the same state lock and must not acquire a PRCB lock while
holding it. The caller is responsible for waiting until a migrating thread has
completed its outgoing switch before placing it on another processor.
The prototype scans runnable entities and uses a single state spinlock; it
does not have Linux's scalable scheduling data structures. Tick reviews and
lock contention need measurement under larger workloads.

The current experimental mapping gives an ordinary thread a weight of
`1024 * min(QuantumReset, 36) / 6` and requests at most six quantum units per
EEVDF slice. For example, a quantum of 6 has weight 1024 and a quantum of 18
has weight 3072, while both request six units. A normal-class process marked
foreground has a minimum weight of 3072, even when foreground and background
quanta are equal. Foreground transitions reweight its existing entities; new
threads inherit the process's recorded state, with the existing NT foreground
base priority used to seed the first thread. This is a prototype policy choice,
not Windows or Linux parity. Explicit NT priority levels take precedence over
weight, and the policy does not identify individual UI threads.

`eevdf_guest_probe.c` is a separate Windows guest program. Build it with
`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror eevdf_guest_probe.c -o eevdf-probe.exe`.
It emits `EEVDF_PROBE` lines through `OutputDebugStringA` and exercises
equal-priority progress, priority precedence, processor-affinity changes, and
event wakeups under CPU load. Stage it on a stopped disposable guest image and
run the same binary under both policies; a desktop boot or a single run is not
a performance comparison.
`eevdf_foreground_probe.c` uses the same build command and runs two pinned
processes at the same effective priority, one with NT foreground information
and one in the background. It reports their service counts without assuming a
particular foreground-quantum configuration.

Both policies use strict NT priority ordering. Priorities 16–31 use round robin
under either policy. At an ordinary priority, the experimental policy chooses
the eligible thread with the earliest virtual deadline. An eligible thread has
received no more than its weighted share relative to the other runnable threads
at that priority. Its deadline is the virtual runtime at the beginning of its
request plus its requested slice divided by its weight. Shorter slices favor
latency; weights affect service over time.

On a wakeup into a nonempty priority queue, the model restores the thread's
previous lag, bounded to one requested slice of virtual time. This preserves a
short sleeper's debt. It is an explicit prototype policy, not a claim about
Windows or Linux behavior. A thread waking into an empty priority level starts
at that level's saved virtual time. Priority changes retain separate saved lag
for each level. The model selects at each simulated step; it does not model
timer resolution, interrupt latency, dispatch costs, SMP, affinity, migration,
foreground detection, or lock inversion.
These must be designed and tested before any kernel integration.

The current dispatcher can ready a thread before the architecture switch
finishes charging its execution time. Domain placement must therefore wait
for that handoff and handle priority changes, processor migration, and the
running thread consistently. Startup threads created before executive pool
initialization also need an explicit policy. No Windows-mirrored structure
field has been identified for the new state (`INFO_NEEDED`).
