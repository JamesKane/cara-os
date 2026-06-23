// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS — the native CaraOS filesystem (docs/CARAFS.md). This header
// is the AUTHORITATIVE on-disk byte layout: every struct here is an
// exact disk image, every offset is pinned by static_asserts below and
// re-verified by the test_carafs_format host unit test. The design
// rationale lives in the doc; the bytes live here.
//
// On-disk integers are little-endian by definition. The implementation
// accesses them through these structs and is therefore only buildable
// on little-endian hosts (compile-checked below) — RV64 and every
// development host qualify. Big-endian-host codec shims are an open
// question in the doc, not a Phase 2 need.
//
// The core (src/logaic/carafs/) is portable pure logic behind the
// CarafsBdev seam: host tools and unit tests bind it to a plain file,
// the kernel binds it to Croi_Nvme_*. It never allocates memory (the
// mounter supplies a cache arena) and never does I/O except through
// the bdev.

#ifndef CARA_CARAFS_H
#define CARA_CARAFS_H

#include <cara/types.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "CaraFS implementation requires a little-endian host (see docs/CARAFS.md §3.1)"
#endif

// ---- Global constants (CARAFS.md §3.1) --------------------------------------

constexpr u32 CARAFS_FORMAT_VERSION = 1;

constexpr u32 CARAFS_MIN_BLOCK_LOG2 = 9;  // 512 B
constexpr u32 CARAFS_MAX_BLOCK_LOG2 = 16; // 64 KiB
constexpr u32 CARAFS_DEF_BLOCK_LOG2 = 12; // 4 KiB default

constexpr u32 CARAFS_NAME_MAX = 255;    // bytes, UTF-8
constexpr u32 CARAFS_COMMENT_MAX = 255; // bytes (V36 parity needs 79)

// The CaraFS epoch is the Amiga epoch: 1978-01-01T00:00:00 UTC.
// Timestamps are u64 nanoseconds since then; 1 DateStamp tick =
// 20 ms = 20,000,000 ns converts exactly.
constexpr u64 CARAFS_EPOCH_UNIX_OFFSET_S = 252460800ull; // 1970 → 1978
constexpr u64 CARAFS_NS_PER_TICK = 20000000ull;

// Block magics: every metadata block self-identifies (§3.1). Values
// are the ASCII tags read as a little-endian u32.
constexpr u32 CARAFS_MAGIC_CNODE = 0x444F4E43u;   // "CNOD"
constexpr u32 CARAFS_MAGIC_BITMAP = 0x4D424143u;  // "CABM"
constexpr u32 CARAFS_MAGIC_BTREE = 0x54424143u;   // "CABT"
constexpr u32 CARAFS_MAGIC_JSB = 0x534A4143u;     // "CAJS"
constexpr u32 CARAFS_MAGIC_JDESC = 0x444A4143u;   // "CAJD"
constexpr u32 CARAFS_MAGIC_JCOMMIT = 0x434A4143u; // "CAJC"
constexpr u32 CARAFS_MAGIC_XATTR = 0x4F584143u;   // "CAXO"

// Superblock magic: 8 ASCII bytes at offset 0 of block 0.
constexpr u8 CARAFS_SB_MAGIC[8] = { 'C', 'a', 'r', 'a', 'F', 'S', '!', '\n' };

// Superblock state.
enum : u32 {
    CARAFS_STATE_CLEAN = 1,
    CARAFS_STATE_DIRTY = 2,
};

// Cnode / dirent object types.
enum : u16 {
    CARAFS_T_FILE = 1,
    CARAFS_T_DIR = 2,
    CARAFS_T_SYMLINK = 3,
};

// Cnode item kinds (§3.4 TLV item area).
enum : u16 {
    CARAFS_ITEM_NAME = 1,
    CARAFS_ITEM_COMMENT = 2,
    CARAFS_ITEM_XATTR = 3,
    CARAFS_ITEM_INLINE_DATA = 4,
    CARAFS_ITEM_INLINE_DIRENTS = 5,
    CARAFS_ITEM_SYMLINK_TARGET = 6,
    CARAFS_ITEM_XATTR_OVERFLOW = 7,
};

// Extent flags.
enum : u16 {
    CARAFS_EXT_UNWRITTEN = (1u << 0),
};

// Btree node flavours.
enum : u8 {
    CARAFS_BT_DIR = 1,
    CARAFS_BT_EXTENT = 2,
};

constexpr u32 CARAFS_INLINE_EXTENTS = 16;

// ---- On-disk structures ------------------------------------------------------
//
// Every struct is an exact disk image (LE host asserted above). The
// crc32c field of each metadata block is computed over the WHOLE
// block with the crc field itself zeroed (Carafs_BlockCrc).

struct CarafsExtent { // 16 B
    u64 start;        // first block; 0 is never a valid extent start
    u32 count;        // blocks
    u16 flags;        // CARAFS_EXT_*
    u16 rsvd;
};
static_assert(sizeof(struct CarafsExtent) == 16);

// Superblock — block 0; backup copy at backup_sb (§3.3).
struct CarafsSuperblock {
    u8 magic[8]; // CARAFS_SB_MAGIC
    u32 crc32c;
    u32 version;
    u32 compat;    // mountable even if unknown bits set
    u32 ro_compat; // unknown bits → mount read-only
    u32 incompat;  // unknown bits → refuse mount
    u32 block_size_log2;
    u32 state;          // CARAFS_STATE_*
    u32 ag_size_blocks; // 8 * (block_size - 64)
    u64 total_blocks;
    u64 journal_start;  // block number of the JSB
    u32 journal_blocks; // including the JSB
    u32 ag_count;
    u64 ag_first_block; // first AG's bitmap block
    u64 root_cnode;
    u64 free_blocks; // advisory; AG headers are exact
    u64 backup_sb;
    u8 uuid[16];
    u8 name_len;
    u8 name[63]; // volume name, UTF-8
    u64 created_ns;
    u64 modified_ns;
};
static_assert(offsetof(struct CarafsSuperblock, crc32c) == 8);
static_assert(offsetof(struct CarafsSuperblock, total_blocks) == 40);
static_assert(offsetof(struct CarafsSuperblock, ag_first_block) == 64);
static_assert(offsetof(struct CarafsSuperblock, uuid) == 96);
static_assert(offsetof(struct CarafsSuperblock, name_len) == 112);
static_assert(offsetof(struct CarafsSuperblock, created_ns) == 176);
static_assert(sizeof(struct CarafsSuperblock) == 192);

// Cnode — one block per object; cnode id == block number (§3.4).
// The TLV item area runs from CARAFS_CNODE_ITEMS_OFF to the end of
// the block.
struct CarafsCnode {
    u32 magic; // CARAFS_MAGIC_CNODE
    u32 crc32c;
    u64 block_no;
    u64 generation;
    u16 type; // CARAFS_T_*
    u16 rsvd16;
    u32 link_count;
    u64 size_bytes; // FILE: length; DIR: entry count; SYMLINK: target len
    u64 blocks_used;
    u64 parent_cnode; // authoritative for DIR; FILE: 0 unless link_count==1
    u64 created_ns;
    u64 modified_ns;
    u64 changed_ns;
    u32 fib_protection; // verbatim V36+ bits (§5)
    u32 flags;
    u64 tree_root;        // FILE: extent tree; DIR: dir tree; 0 = inline
    u32 n_inline_extents; // FILE only
    u32 item_bytes;       // bytes used in the item area
    struct CarafsExtent ext[CARAFS_INLINE_EXTENTS];
    // u8 items[] to end of block
};
static_assert(offsetof(struct CarafsCnode, size_bytes) == 32);
static_assert(offsetof(struct CarafsCnode, fib_protection) == 80);
static_assert(offsetof(struct CarafsCnode, tree_root) == 88);
static_assert(offsetof(struct CarafsCnode, ext) == 104);
static_assert(sizeof(struct CarafsCnode) == 360);
constexpr u32 CARAFS_CNODE_ITEMS_OFF = 360;

// Item header: payload follows immediately; records are packed at
// stride align8(4 + len).
struct CarafsItem {
    u16 kind; // CARAFS_ITEM_*
    u16 len;  // payload bytes
    // u8 payload[len], then pad to 8-byte stride
};
static_assert(sizeof(struct CarafsItem) == 4);

// Dirent — inside CARAFS_ITEM_INLINE_DIRENTS payloads and dir-tree
// leaf bodies. Sorted by (hash, seq). Record stride =
// align8(20 + name_len) (§3.6).
struct CarafsDirent {
    u64 hash; // Carafs_NameHash of the folded name
    u64 cnode;
    u8 type; // CARAFS_T_* (cheap fib_DirEntryType during ExNext)
    u8 name_len;
    u8 seq; // collision sequence among equal hashes
    u8 rsvd;
    // u8 name[name_len] at byte CARAFS_DIRENT_BASE, pad record to 8
};
// The packed prefix is 20 bytes (sizeof rounds up to alignment — use
// CARAFS_DIRENT_BASE, never sizeof, for record math).
static_assert(offsetof(struct CarafsDirent, type) == 16);
static_assert(offsetof(struct CarafsDirent, rsvd) == 19);
constexpr u32 CARAFS_DIRENT_BASE = 20;

// Btree node (§3.6/§3.7). Shared by directory trees (flavour DIR:
// leaves hold packed dirents) and extent trees (flavour EXTENT:
// leaves hold CarafsExtentRec). Interior nodes of both flavours hold
// CarafsBtInteriorRec records; a record's key is the smallest key in
// its child's subtree.
struct CarafsBtNode {
    u32 magic; // CARAFS_MAGIC_BTREE
    u32 crc32c;
    u64 block_no;
    u8 level; // 0 = leaf
    u8 flavour;
    u16 n_records;
    u32 used_bytes; // body bytes in use
    u8 rsvd[8];
    // u8 body[] to end of block
};
static_assert(sizeof(struct CarafsBtNode) == 32);
constexpr u32 CARAFS_BT_BODY_OFF = 32;

struct CarafsBtInteriorRec { // 24 B
    u64 key_hi;              // dir: name hash; extent: file block offset
    u64 key_lo;              // dir: seq; extent: 0
    u64 child;
};
static_assert(sizeof(struct CarafsBtInteriorRec) == 24);

struct CarafsExtentRec { // 24 B — extent-tree leaf record
    u64 file_off;        // file block offset
    u64 start;
    u32 count;
    u16 flags;
    u16 rsvd;
};
static_assert(sizeof(struct CarafsExtentRec) == 24);

// Allocation-group bitmap block (§3.8): 64-byte header + the bits.
// AG n covers ag_size_blocks blocks starting at its own bitmap block;
// bit 0 (the bitmap block itself) is always allocated. Bit set =
// block allocated.
struct CarafsAgHeader {
    u32 magic; // CARAFS_MAGIC_BITMAP
    u32 crc32c;
    u64 block_no;
    u32 free_count; // exact
    u32 rotor;      // allocator cursor (advisory)
    u8 rsvd[40];
    // u8 bits[block_size - 64]
};
static_assert(sizeof(struct CarafsAgHeader) == 64);
constexpr u32 CARAFS_AG_BITS_OFF = 64;

// Journal (§3.9). Block 0 of the journal region is the JSB; the rest
// is a circular log of DESC | images... | COMMIT transactions.
struct CarafsJsb {
    u32 magic; // CARAFS_MAGIC_JSB
    u32 crc32c;
    u64 seq;  // sequence number of the first unreplayed transaction
    u32 head; // log-block offset (1..journal_blocks-1) of that txn
    u32 rsvd;
};
static_assert(sizeof(struct CarafsJsb) == 24);

struct CarafsJdesc {
    u32 magic; // CARAFS_MAGIC_JDESC
    u32 crc32c;
    u64 seq;
    u32 n_blocks;
    u32 rsvd;
    // u64 targets[n_blocks] — home block numbers of the images
};
static_assert(sizeof(struct CarafsJdesc) == 24);
constexpr u32 CARAFS_JDESC_TARGETS_OFF = 24;

struct CarafsJcommit {
    u32 magic; // CARAFS_MAGIC_JCOMMIT
    u32 crc32c;
    u64 seq;
    u32 txn_crc; // CRC-32C chained over the image blocks, seed 0
    u32 n_blocks;
};
static_assert(sizeof(struct CarafsJcommit) == 24);

// Xattr overflow block (§3.10): header + packed XATTR items.
struct CarafsXattrBlock {
    u32 magic; // CARAFS_MAGIC_XATTR
    u32 crc32c;
    u64 block_no;
    u64 next; // chain, 0 = end
    u32 item_bytes;
    u32 rsvd;
    // u8 items[] to end of block
};
static_assert(sizeof(struct CarafsXattrBlock) == 32);

// ---- Format helpers (src/logaic/carafs/format.c) ----------------------------

// CRC-32C (Castagnoli), software table-driven, no third-party code.
// Seed 0 for standalone use; chain by passing the previous result.
[[nodiscard]] u32 Carafs_Crc32c(u32 seed, const void *data, usize len);

// Block checksum: CRC-32C over the whole block with the crc field
// (at byte crc_off, 4 bytes) treated as zero. Works on a const block.
[[nodiscard]] u32 Carafs_BlockCrc(const void *block, u32 block_size, u32 crc_off);

// Case fold for name comparison and hashing: ASCII A–Z → a–z,
// bytewise (safe on UTF-8 multibyte sequences; CARAFS.md §3.5).
[[nodiscard]] u8 Carafs_FoldByte(u8 c);

// FNV-1a 64 over the folded name bytes (§3.5).
[[nodiscard]] u64 Carafs_NameHash(const void *name, u32 len);

// Folded (case-insensitive) name equality.
[[nodiscard]] bool Carafs_NameEq(const void *a, u32 a_len, const void *b, u32 b_len);

// Name validity: 1..CARAFS_NAME_MAX bytes, no '/', ':', or NUL.
[[nodiscard]] bool Carafs_NameValid(const void *name, u32 len);

// Item-record stride helper: align8(sizeof header + payload len).
[[nodiscard]] static inline u32 Carafs_Align8(u32 n)
{
    return (n + 7u) & ~7u;
}

// ---- Block device seam (CARAFS.md §4) ----------------------------------------
//
// One bdev block == one filesystem block: whoever constructs the bdev
// does the LBA math (the kernel NVMe binding maps one FS block to
// block_size/512 LBAs; the host file binding maps it to a file
// offset). The core does no I/O except through these calls.

struct CarafsBdev {
    void *ctx;
    u32 block_size; // == the volume's filesystem block size
    u64 n_blocks;
    int (*read)(void *ctx, u64 block, u32 n, void *buf);
    int (*write)(void *ctx, u64 block, u32 n, const void *buf);
    int (*flush)(void *ctx); // durability barrier (NVMe Flush / fsync)
};

// Memory-backed bdev — the unit-test workhorse (a 16 MiB volume is
// just a 16 MiB host allocation; also usable from kernel tests).
// `mem` must hold n_blocks × block_size bytes; flush is a no-op.
struct CarafsMemBdev {
    u8 *mem;
    u32 block_size;
    u64 n_blocks;
};
void Carafs_MemBdev_Init(struct CarafsBdev *out, struct CarafsMemBdev *state, void *mem,
                         u32 block_size, u64 n_blocks);

// ---- mkfs / fsck (epic F1) ---------------------------------------------------

struct CarafsMkfsOpts {
    u32 block_size_log2; // 0 → CARAFS_DEF_BLOCK_LOG2
    const void *name;    // volume name (UTF-8); required
    u32 name_len;        // 1..63
    const u8 *uuid;      // 16 bytes, or nullptr → derived from name+now
    u64 now_ns;          // CaraFS-epoch timestamp for created_ns
};

// Build an empty volume on the bdev: superblock, journal (JSB at
// journal_start, log idle), AG bitmap chain, empty root directory
// cnode, backup superblock. `scratch` must hold >= 1 filesystem
// block (the core never allocates). Returns CARA_EINVAL for bad
// geometry / name / scratch, CARA_ERANGE if the device is too small
// (~1 MiB + journal), or the bdev's error code on I/O failure.
[[nodiscard]] int Carafs_Mkfs(struct CarafsBdev *bdev, const struct CarafsMkfsOpts *opts,
                              void *scratch, usize scratch_bytes);

struct CarafsFsckReport {
    u32 errors;
    u32 warnings;
    u64 blocks_checked;
    // First error, as a short static string + the block it hit.
    const char *first_error; // nullptr if errors == 0
    u64 first_error_block;
};

// Read-only structural check: superblock (+ backup), JSB, every AG
// header (magic / crc / self-address / exact free_count vs popcount),
// root cnode sanity. Grows cross-checks in later epics. `scratch`
// must hold at least 2 filesystem blocks. Returns CARA_EOK when the
// volume was checkable (rep->errors may still be nonzero); CARA_EIO /
// CARA_EINVAL when it couldn't even read the superblock.
[[nodiscard]] int Carafs_Fsck(struct CarafsBdev *bdev, void *scratch, usize scratch_bytes,
                              struct CarafsFsckReport *rep);

// ---- Mount (epics F2+) -------------------------------------------------------

// Cache entry flags (internal, exposed for sizing math only).
constexpr u32 CARAFS_CACHE_MIN_BLOCKS = 16;
// One transaction's metadata blocks. Bounded so a DESC record's target
// list fits in a single block at the smallest supported block size
// (512 B): CARAFS_JDESC_TARGETS_OFF + N*8 <= 512. A real op touches far
// fewer (a deep tree split is ~25 blocks).
constexpr u32 CARAFS_TXN_MAX_BLOCKS = 60;
static_assert(CARAFS_JDESC_TARGETS_OFF + CARAFS_TXN_MAX_BLOCKS * sizeof(u64) <=
              (1u << CARAFS_MIN_BLOCK_LOG2));

struct CarafsCacheEnt {
    u64 block; // ~0ull = empty slot
    u32 flags;
    u32 pins;
    u32 lru_prev, lru_next;
    u32 hash_next; // bucket chain, ~0u = end
    u32 rsvd;
    u8 *data;
};

struct CarafsMountOpts {
    void *cache_mem;   // caller-owned arena; the core never allocates
    usize cache_bytes; // must fit >= CARAFS_CACHE_MIN_BLOCKS blocks + overhead
    bool readonly;
    u64 (*now_ns)(void *ctx); // CaraFS-epoch clock; nullptr → timestamps stay 0
    void *now_ctx;
};

struct CarafsMount {
    struct CarafsBdev *bdev;
    u32 block_size;
    bool readonly;
    bool mounted;
    struct CarafsSuperblock sb; // in-memory copy; block 0 rewritten via txns

    // Block cache (carved from the mount arena).
    u32 n_ents;
    struct CarafsCacheEnt *ents;
    u32 *buckets;
    u32 n_buckets;
    u32 lru_head, lru_tail;
    u8 *block_mem;

    // Current transaction (metadata blocks awaiting commit).
    bool in_txn;
    u32 txn_n;
    u64 txn_blocks[CARAFS_TXN_MAX_BLOCKS];
    struct CarafsSuperblock sb_at_txn; // snapshot; restored on abort

    // Journal (F4, §3.9): a circular WAL over the journal region. Log
    // offsets are 1..journal_blocks-1 (offset 0 is the JSB); physical
    // block = journal_start + offset. [j_head, j_tail) holds committed
    // but un-checkpointed transactions; j_head/j_head_seq mirror the
    // JSB. j_scratch builds DESC/COMMIT records, j_image reads images
    // during replay — both carved from the mount arena.
    u32 j_log_blocks; // journal_blocks - 1 (usable log offsets)
    u32 j_head;       // oldest un-checkpointed txn (== JSB head)
    u64 j_head_seq;   // its sequence number (== JSB seq)
    u32 j_tail;       // write cursor (where the next txn goes)
    u64 j_next_seq;   // sequence number for the next txn
    u8 *j_scratch;
    u8 *j_image;
    u64 stat_checkpoints; // checkpoints performed (test/observability)

    // Clock.
    u64 (*now_ns)(void *ctx);
    void *now_ctx;

    // Stats — the million-entry test asserts on block reads, not time.
    u64 stat_bdev_reads;
    u64 stat_bdev_writes;
    u64 stat_cache_hits;
};

// Mount: validate superblock (magic, crc, version, incompat mask,
// geometry vs bdev), set state DIRTY on the first write (not at
// mount). Journal replay lands in F4. Returns CARA_EBADMAGIC /
// CARA_EBADVERSION / CARA_EINVAL / CARA_ENOMEM (arena too small).
[[nodiscard]] int Carafs_Mount(struct CarafsMount *m, struct CarafsBdev *bdev,
                               const struct CarafsMountOpts *opts);

// Flush everything, mark the superblock CLEAN, detach.
[[nodiscard]] int Carafs_Unmount(struct CarafsMount *m);

// Write back all dirty metadata + flush (durability point without
// unmounting).
[[nodiscard]] int Carafs_Sync(struct CarafsMount *m);

// ---- Cnodes and file content (epic F2) ---------------------------------------
//
// A cnode id IS its block number (§3.4). F2 exposes anonymous cnode
// and file-content operations; F3's directory layer links names to
// them. Every mutating call brackets its own transaction and is
// durable when it returns (the commit appends the metadata images to
// the WAL and flushes; home writes are lazy, §3.9).

struct CarafsStat {
    u64 cnode;
    u64 generation;
    u16 type; // CARAFS_T_*
    u32 link_count;
    u64 size_bytes;
    u64 blocks_used;
    u64 parent_cnode;
    u64 created_ns;
    u64 modified_ns;
    u64 changed_ns;
    u32 fib_protection;
    u32 flags;
};

// Allocate and initialise a cnode of `type`. `hint` is a block
// number steering AG locality (0 → volume start); F3 passes the
// parent directory's cnode. link_count starts at 1 — real link
// accounting belongs to the directory layer.
[[nodiscard]] int Carafs_CnodeCreate(struct CarafsMount *m, u16 type, u64 hint, u64 *cnode_out);

// Free the cnode and all its storage (inline data, extents, extent
// tree). The block is rewritten as a tombstone — generation bumped,
// link_count 0 — so stale references are detectable (best-effort:
// reuse as a data block destroys the tombstone).
[[nodiscard]] int Carafs_CnodeDelete(struct CarafsMount *m, u64 cnode);

// CARA_ENOENT for a tombstone, CARA_EBADMAGIC for a block that is
// not a valid cnode.
[[nodiscard]] int Carafs_CnodeStat(struct CarafsMount *m, u64 cnode, struct CarafsStat *st);

// File content. Read returns min(len, size - off) bytes via
// *read_out (0 at or past EOF); holes and UNWRITTEN extents read as
// zeros. Write extends the file (sparse when off > size), promoting
// inline data → inline extents → extent tree transparently; a
// failed write aborts its whole transaction.
[[nodiscard]] int Carafs_FileRead(struct CarafsMount *m, u64 cnode, u64 off, void *buf, usize len,
                                  usize *read_out);
[[nodiscard]] int Carafs_FileWrite(struct CarafsMount *m, u64 cnode, u64 off, const void *buf,
                                   usize len);

// ---- Directories and links (epic F3) ----------------------------------------
//
// Names link to cnodes through a directory's entries, which escalate
// transparently: an INLINE_DIRENTS item in the cnode → one dirent leaf
// block → a (name_hash, collision_seq)-keyed B+tree (CARAFS.md §3.6).
// Lookup is case-insensitive under the ASCII fold (§3.5). A directory's
// size_bytes is its entry count. Every mutator brackets its own
// transaction and is durable on return (pre-F4 commit = write + flush).

// A resume token for Carafs_DirNext. Zero-initialise to start a listing;
// it survives concurrent insertion/removal elsewhere (§3.6 cursor rule).
struct CarafsDirCursor {
    u64 hash;
    u8 seq;
    bool started;
};

// One entry surfaced by Carafs_DirNext: the target and its name. The
// name is not NUL-terminated; name_len bytes of `name` are valid.
struct CarafsDirEntry {
    u64 cnode;
    u16 type; // CARAFS_T_*
    u8 name_len;
    u8 name[CARAFS_NAME_MAX];
};

// Look up `name` in `dir` (case-insensitive). Fills *cnode_out / *type_out
// (either may be null). CARA_ENOENT if absent, CARA_EINVAL if `dir` is
// not a directory.
[[nodiscard]] int Carafs_DirLookup(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                   u64 *cnode_out, u16 *type_out);

// Create a new object of `type` (FILE / DIR / SYMLINK) named `name` in
// `dir` and link it: allocates its cnode, stores the NAME item and
// parent_cnode, inserts the dirent. CARA_EEXIST if the name is taken.
// A SYMLINK created here has no target yet — use Carafs_DirSymlink.
[[nodiscard]] int Carafs_DirCreate(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                   u16 type, u64 *cnode_out);

// Create a symbolic link named `name` whose target is the byte string
// [target, target+target_len). CARA_EEXIST if the name is taken.
[[nodiscard]] int Carafs_DirSymlink(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                    const void *target, u32 target_len, u64 *cnode_out);

// Hard-link an existing file cnode under a new name (bumps link_count).
// CARA_EEXIST if the name is taken; CARA_EINVAL if `target` is not a file
// (no directory hard links, §3.11).
[[nodiscard]] int Carafs_DirLink(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                 u64 target);

// Remove `name` from `dir`. Decrements the target's link_count; the
// cnode and all its storage are freed when it reaches 0. A directory
// must be empty (CARA_ENOTEMPTY otherwise). CARA_ENOENT if absent.
[[nodiscard]] int Carafs_DirRemove(struct CarafsMount *m, u64 dir, const void *name, u32 name_len);

// Iterate `dir` in key order. *cur is zero-initialised on the first
// call. CARA_EOK + *out for each entry, CARA_ENOENT past the last one.
[[nodiscard]] int Carafs_DirNext(struct CarafsMount *m, u64 dir, struct CarafsDirCursor *cur,
                                 struct CarafsDirEntry *out);

// Copy a symlink's stored target into buf (up to buflen bytes); *len_out
// gets the full target length. CARA_EINVAL if `cnode` is not a symlink.
[[nodiscard]] int Carafs_SymlinkRead(struct CarafsMount *m, u64 cnode, void *buf, usize buflen,
                                     usize *len_out);

// ---- Extended attributes (epic L11; CARAFS.md §3.10) -------------------------
//
// Named attributes stored on the object's cnode — the `cara.icon` xattr
// backs icon.library, and `cara.*` is reserved / `user.*` free-form. v0
// is inline-only: all of a cnode's attributes share one
// CARAFS_ITEM_XATTR item, a packed list of records
//   { u8 name_len; u8 flags; u16 val_len; name[name_len]; val[val_len] }.
// A value that will not fit the cnode's inline item area returns
// CARA_EOVERFLOW (the XATTR_OVERFLOW chain is a later slice). Names are
// 1..CARAFS_XATTR_NAME_MAX bytes and matched case-sensitively; values are
// 0..65535 bytes.

constexpr u32 CARAFS_XATTR_NAME_MAX = 255;
constexpr u32 CARAFS_XATTR_VALUE_MAX = 65535;

// Read attribute `name` of `cnode` into buf (up to buflen bytes). *len_out
// (non-null) gets the full value length even when it exceeds buflen, so a
// caller can size a second call. CARA_ENOENT if the attribute is absent.
[[nodiscard]] int Carafs_XattrGet(struct CarafsMount *m, u64 cnode, const void *name, u32 name_len,
                                  void *buf, usize buflen, usize *len_out);

// Set (create or replace) attribute `name` of `cnode`. CARA_EOVERFLOW if
// it will not fit inline. Brackets its own transaction; durable on return.
[[nodiscard]] int Carafs_XattrSet(struct CarafsMount *m, u64 cnode, const void *name, u32 name_len,
                                  const void *val, u32 val_len);

// Remove attribute `name` of `cnode`. CARA_ENOENT if absent. Own txn.
[[nodiscard]] int Carafs_XattrRemove(struct CarafsMount *m, u64 cnode, const void *name,
                                     u32 name_len);

#endif
