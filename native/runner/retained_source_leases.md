# Retained native Source child leases (staged controller)

This module is a staged prerequisite for reusing worker fixtures that own a live
CEF browser process. It does not change current reuse eligibility. Existing
anonymous worker reuse continues to reject any child of a retained SDK host.
No browser, Source startup, Source idle or Source close parity is established by
these component tests.

## Ownership and registration contract

The runner owns a fixed-capacity controller (8 outstanding registrations, 256
historical tickets). It reserves a ticket before the SDK may spawn Source. Each
ticket binds the actual retained host lease/index/PID/kernel start identity and
native worker-fixture catalog/declaration/token/test/generation. Native fixture
resolution supplies the effective declaration version; author aliases cannot
select that identity. The native fixture must still be Starting or Ready in the
actual FixtureOwners controller. Public NativeFixtureInfo need not grow a
caller-controlled token field.

A private fixture-setup author context only marshals the native ticket and an
inherited runner-owned AF_UNIX SOCK_SEQPACKET endpoint into launchNativeBrowser.
The runner enables SO_PASSCRED before any child can emit. Actual native Source
emits SOURCE_START version1/ticket/PID/parent/start identity. The receiver checks
kernel SCM_CREDENTIALS, same UID/GID, original live host parent/start identity,
Source PID/start identity and pinned executable device/inode. PID identity is
held and rechecked with a native pidfd during observation. Executable equality
alone never establishes a lease. The caller retains the actual original host
object/waitpid ownership throughout verification. This is infrastructure receipt
authentication, not a hostile-author sandbox or an import/tamper-proof claim.

Source must mark the inherited endpoint CLOEXEC before any CEF subprocess launch;
CEF subprocess entry must not retain an endpoint. Every unknown/unregistered child
continues to reject reuse. The dedicated receiver is not an adopted-child reaper.
The single existing ProjectDescendants/DescendantOwner domain remains the only
runner owner of adopted waitpid receipts; the SDK/Node process retains its own
normal direct-child waitpid ownership while it lives.

## Accepted work, idle and close

Registration alone never enables reuse. The controller has a bounded ledger of
64 genuine accepted native operations and monotonic per-Source emission IDs.
Each backend admission must reserve its operation before actual accepted work;
only its actual physical backend/Inspector receipt retires that operation.
Cancellation and timeout retain pending work. Out-of-order physical settlements
are supported; replay, missing operation, foreign ticket and stale emissions are
rejected. The future Source adapter must cover all real accepted native work,
including startup/backend/Inspector ownership, rather than inventing zero-valued
diagnostic counters. Until that adapter exists, no Source idle is established.

An exact idle request has its own nonce and at most 1000ms inside the original
global budget. An idle receipt requires the current accepted watermark and zero
pending physical operations. New accepted work invalidates established idle.
A genuine matching idle receipt during Stop/expiry/close is observed once as a
canceled receipt without enabling idle or discharging ownership.

Close has a separate native nonce and bounded physical-cleanup request. It may
run after the global author budget ends; it never admits new author work or
extends the original global deadline. Close ACK is distinct from process exit
and never decrements charged child ownership. Native pidfd observation identifies
actual exit of the exact Source incarnation. Final ticket retirement additionally
requires the exact host's real waitpid, drained registration channel, and actual
shared descendant batch retirement. Pending operations remain failure history
when process death invalidates them; no settled or close ACK is fabricated.
Unmaterialized startup tickets also require those host/domain/channel receipts
before release. A host with a genuine retirement receipt cannot reserve again.

## Focused evidence and remaining integration

The policy component has 22 controlled receipt groups for ownership/version
mismatch, startup/late-start/deadlines, capacity/history, accepted-work ordering,
idle/late canceled ACKs, bounded close, process exit and exact host/domain release.
These literal receipts make no OS or browser claim.

The independent kernel component has seven actual groups: native exec child
credentials/parent/start/executable observation; rejection of forwarded own-PID
bytes, foreign parent start/executable/ticket, truncated/noncanonical/EOF packets;
CLOEXEC across a real exec descendant; and SCM_RIGHTS rejection without leaking
received descriptors. The component observes exact pidfd exit and its original
native parent performs exact known-PID waitpid. Optimized and ASan/UBSan/LSan runs
pass with stderr0. The first authored strict indentation warnings are preserved.

Following work remains explicit: actual native Source startup emission, complete
accepted-work/Inspector ledger bindings, native idle/close receipt transport,
fixture-resolver/SDK setup marshalling, exact known-child inventory/cohort checks,
combined report validation, and genuine CEF worker-fixture process proofs. No
permissive inventory or eligibility switch belongs in this prerequisite module.
