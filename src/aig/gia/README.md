# GIA — Scalable AIG Package

This directory holds GIA, one of the And-Inverter Graph representations in ABC and the one most of
the modern code is written against. This document is the roadmap: it explains what the package is
responsible for, where it sits in the system, how an object and the manager are laid out, how the
graph is identified and walked, which fields carry more than one meaning, how the three central data
flows run, and which invariants, traps and limits a maintainer has to respect. Every factual claim
carries an inline citation of the form `path:Lnnn` or `path:Lnnn-Lmmm`, naming the file and the line
or range that establishes it. The bit-level treatment of the twelve-byte object encoding lives in a
separate document, [`./ENCODING.md`](./ENCODING.md); how to *call* the package from client code
lives in the root [`../../../readmeaig`](../../../readmeaig).

Contents:

- [1. What GIA Is and Why It Exists](#1-what-gia-is-and-why-it-exists)
- [2. Where GIA Sits in ABC](#2-where-gia-sits-in-abc)
- [3. Package Map and Reading Guide](#3-package-map-and-reading-guide)
- [4. The Object](#4-the-object)
- [5. Fanin Encoding: Relative Offsets](#5-fanin-encoding-relative-offsets)
- [6. The Manager](#6-the-manager)
- [7. Identity, Kinds, and Traversal](#7-identity-kinds-and-traversal)
- [8. Overloaded and Reused Fields](#8-overloaded-and-reused-fields)
- [9. Key Data Flows](#9-key-data-flows)
- [10. Invariants](#10-invariants)
- [11. Gotchas](#11-gotchas)
- [12. Limitations](#12-limitations)
- [13. Observed Discrepancies and Dead Code](#13-observed-discrepancies-and-dead-code)
- [14. Where to Look Next](#14-where-to-look-next)

## 1. What GIA Is and Why It Exists

The package describes itself in its own file banner, at `src/aig/gia/gia.h:L1-19`. Two of the
bracketed tags state the scope: `PackageName [Scalable AIG package.]` at `src/aig/gia/gia.h:L7` and
`Synopsis    [External declarations.]` at `src/aig/gia/gia.h:L9`. The header is the package's public
C contract; the implementation is spread over the rest of the directory.

An And-Inverter Graph is a directed acyclic graph in which every internal node is a two-input
conjunction and every edge carries an optional inversion, so a single node type plus edge
complementation suffices to represent any Boolean function. GIA's internal nodes are exactly that:
the two fanins of an AND object each carry their own complement bit, `fCompl0` at
`src/aig/gia/gia.h:L80` and `fCompl1` at `src/aig/gia/gia.h:L85`. Inputs and outputs of the graph
are terminal objects rather than gates, and the graph has one distinguished constant object.

The word *scalable* in the banner is a claim about memory, and the numbers behind it are small
enough to state exactly. Sizes here were measured by compiling a probe against the real header
rather than deduced from the declarations:

```bash
printf '#include <stdio.h>\n#include "aig/gia/gia.h"\nint main(void){printf("%%zu %%zu %%zu %%zu\\n",sizeof(Gia_Obj_t),sizeof(Gia_Man_t),sizeof(Gia_Rpr_t),sizeof(Gia_Plc_t));return 0;}\n' > /tmp/probe.c
gcc -I src -DABC_USE_STDINT_H=1 -o /tmp/probe /tmp/probe.c -lm && /tmp/probe
```

It prints `12 1136 4 4`. One object is therefore twelve bytes, and one manager — the whole
bookkeeping structure, whatever the graph size — is 1136 bytes. There is no per-object allocation
and no per-edge allocation at all: objects live in a single flat array owned by the manager,
`p->pObjs`, declared at `src/aig/gia/gia.h:L105`, allocated zero-filled in one call,

```c
    p->pObjs = ABC_CALLOC( Gia_Obj_t, nObjsMax );
```

at `src/aig/gia/giaMan.c:L63`, and grown by doubling when it fills, at
`src/aig/gia/gia.h:L686`. Edges are not stored as separate records: each object stores its fanins as
two backward offsets inside its own twelve bytes, which is the subject of section 5.

Two consequences of that layout shape everything else in the package. An object identifier is an
index into the flat array, so identifiers are dense, small and usable as a key into any parallel
side array — the manager keeps several, listed in section 6. And because the array is contiguous and
grows by reallocation, the encoding cannot hold addresses; it holds differences instead.

## 2. Where GIA Sits in ABC

GIA is one of seven coexisting AIG packages, not *the* AIG package. `src/aig/` contains exactly
seven package directories — `aig`, `gia`, `hop`, `ioa`, `ivy`, `miniaig`, `saig` — and no files of
its own. The other six remain in the tree and in use; GIA is the representation the newer
combinational and sequential engines are written against, and it is the one reached by the
`&`-prefixed commands.

Above GIA sits ABC's command layer. The frame carries a GIA manager alongside the classic network:
`Gia_Man_t * pGia;` is declared at `src/base/main/mainInt.h:L118`, with three further slots for
copies at `src/base/main/mainInt.h:L119-121`, and the frame accessors are declared at
`src/base/main/main.h:L86-87` as `Abc_FrameUpdateGia` and `Abc_FrameGetGia`. Commands that operate
on that manager are registered in the `"ABC9"` command group in `src/base/abci/abc.c`; a count of
the `"ABC9"` group name in that file gives 233 registrations.

Two of those commands are the bridge to the classic `Abc_Ntk_t` representation:

| Command | Registered at | Implementation | How it crosses over |
|---|---|---|---|
| `&get` | `src/base/abci/abc.c:L1230` | `Abc_CommandAbc9Get` at `src/base/abci/abc.c:L33791` | `Abc_NtkStrash` `src/base/abci/abc.c:L33836`, then `Abc_NtkToDar` `src/base/abci/abc.c:L33837`, then `Gia_ManFromAig` `src/base/abci/abc.c:L33839` and `src/base/abci/abc.c:L33854`, then `Abc_FrameUpdateGia` `src/base/abci/abc.c:L33875` |
| `&put` | `src/base/abci/abc.c:L1231` | `Abc_CommandAbc9Put` at `src/base/abci/abc.c:L33903` | `Abc_NtkFromMappedGia` `src/base/abci/abc.c:L33946`, `Abc_NtkFromCellMappedGia` `src/base/abci/abc.c:L33948`, or `Gia_ManToAig( pAbc->pGia, 0 )` `src/base/abci/abc.c:L33953` |

The conversion functions themselves belong to GIA and are declared in the package's bridge header:
`Gia_ManFromAig` at `src/aig/gia/giaAig.h:L53` and `Gia_ManToAig` at `src/aig/gia/giaAig.h:L57`, in
a 78-line header whose declarations all sit under a single group marker naming `giaAig.c`, at
`src/aig/gia/giaAig.h:L52`. Of the package's seven headers, `giaAig.h` is the only one that mentions
`Aig_Man_t` at all; the central header `src/aig/gia/gia.h` never names it, so the declared contract of
GIA itself does not depend on the older representation.

The command layer is named here and no further: this document covers the package, not the commands
that drive it.

```mermaid
graph LR
    CMD["ABC9 commands in abc.c"]
    NTK["Abc_Ntk_t"]
    GIA["Gia_Man_t in src/aig/gia"]
    FRAME["Abc_Frame_t member pGia"]
    SIBS["aig, hop, ioa, ivy, miniaig, saig"]
    CMD --> NTK
    CMD --> GIA
    NTK -- "&get - Gia_ManFromAig" --> GIA
    GIA -- "&put - Gia_ManToAig" --> NTK
    FRAME --- GIA
    GIA -.- SIBS
```

Diagram: GIA's position in ABC. From `src/base/main/mainInt.h:L118`,
`src/base/abci/abc.c:L1230-1231`, `src/aig/gia/giaAig.h:L53`, `src/aig/gia/giaAig.h:L57` and the
directory listing of `src/aig/`.

## 3. Package Map and Reading Guide

The package is one flat directory with no subdirectories. Counted at the commit named in section 14,
it holds 135 entries: 123 `.c` files, 4 `.cpp` files, 7 `.h` files and one `module.make`. The
Markdown documents — this file and [`./ENCODING.md`](./ENCODING.md) — sit alongside them and are not
part of that source count. `src/aig/gia/module.make` enumerates 119 build sources, being 115 of the
`.c` files and all 4 `.cpp` files, the latter named `giaDecGraph.cpp`, `giaRrr.cpp`,
`giaTransduction.cpp` and `giaTtopt.cpp`. The seven headers, by line count, are `gia.h` at 1880
lines, `giaTransduction.h` at 1768, `giaNewBdd.h` at 869, `giaNewTt.h` at 292, `giaCSatP.h` at 117,
`giaAig.h` at 78 and `giaIiff.h` at 54.

A census caveat, since the two figures above differ: `module.make` lists 115 `.c` files while 123
are present, so eight `.c` files are in the directory but not in the build. Matching each filename
against `module.make` with an anchored pattern identifies them as `gia.c`, `giaCTas2.c`,
`giaConstr.c`, `giaGiarf.c`, `giaHcd.c`, `giaMffc.c`, `giaProp.c` and `giaSat.c`. Nothing here was
changed on that account.

Because the directory is flat, the header supplies the only navigational structure the package has.
Its FUNCTION DECLARATIONS region, which opens at `src/aig/gia/gia.h:L1273-1275`, is partitioned by
group markers, one before each implementing file's block of declarations, each padded out to a
fixed width — the first reads
`/*=== giaAiger.c ===========================================================*/` at
`src/aig/gia/gia.h:L1277` and the last is `giaBound.c` at
`src/aig/gia/gia.h:L1837`. Counting those marker lines gives 59, but only 58 distinct implementing
files, because `giaCTas.c` is marked twice, at `src/aig/gia/gia.h:L1316` and
`src/aig/gia/gia.h:L1824`. Between those markers sit the header's 516 `extern` declarations, so the
marker list doubles as a table of contents: to find where a declared function lives, scan up from it
to the nearest marker.

Three details of that marker list are worth knowing before relying on it. One marker carries a
leading space, at `src/aig/gia/gia.h:L1555`. Three name `.cpp` files: `giaTtopt.cpp` at
`src/aig/gia/gia.h:L1813`, `giaTransduction.cpp` at `src/aig/gia/gia.h:L1817` and `giaRrr.cpp` at
`src/aig/gia/gia.h:L1821`. And one names `giaDecGraph.c` at `src/aig/gia/gia.h:L1833` while the file
present in the directory is `giaDecGraph.cpp`.

A small core of the package defines and maintains the representation; the remaining hundred-plus
files implement algorithms over it — mapping, balancing, rewriting, simulation, equivalence
checking, AIGER input and output, and the rest. For the representation itself, five files carry
almost all of it:

| Read | Lines | What it holds |
|---|---|---|
| `src/aig/gia/gia.h` | 1880 | The whole declared contract: both structs, the two sentinels, 314 `static inline` primitives, 62 `ForEach` iterator macros, 68 `#define` directives, 516 `extern` declarations, the fourteen kind predicates and the sixteen object constructors |
| `src/aig/gia/giaMan.c` | 2415 | Manager lifecycle and ownership: `Gia_ManStart` `src/aig/gia/giaMan.c:L57`, `Gia_ManStop` `src/aig/gia/giaMan.c:L82`, `Gia_ManMemory` `src/aig/gia/giaMan.c:L196`, `Gia_ManStopP` `src/aig/gia/giaMan.c:L226`, `Gia_ManSetRegNum` `src/aig/gia/giaMan.c:L766` |
| `src/aig/gia/giaHash.c` | 1145 | Structural hashing end to end: the key function, the chain walk, the table lifecycle, the AND, real XOR and real MUX entry points, and the canonical rebuild |
| `src/aig/gia/giaDup.c` | 6676 | Duplication and rebuild. Counting definitions that return a new manager, with `grep -cE '^Gia_Man_t \* Gia_ManDup' src/aig/gia/giaDup.c`, gives 90 entry points |
| `src/aig/gia/giaUtil.c` | 3573 | The shared field-reuse protocol: the traversal-identifier counter, and the mark, value and phase helpers |

Two more files are cited in this document as corroborating evidence without being central:
`src/aig/gia/giaScl.c`, whose `Gia_ManCleanup` establishes the ownership convention of section 9, and
`src/aig/gia/giaFront.c`, which converts a frontier encoding back into the relative offsets of
section 5.

## 4. The Object

`Gia_Obj_t` is declared at `src/aig/gia/gia.h:L76-90` as nine members packed into three 32-bit words,
with blank lines at `src/aig/gia/gia.h:L83` and `src/aig/gia/gia.h:L88` marking the word boundaries.
The measured `sizeof(Gia_Obj_t)` is 12, so the three words are the whole object and nothing is left
for padding.

| Field | Width | Word | Declared | What it holds |
|---|---|---|---|---|
| `iDiff0` | 29 bits | 1 | `src/aig/gia/gia.h:L79` | Backward offset, in units of `Gia_Obj_t`, from this object to its first fanin; `GIA_NONE` when there is no first fanin |
| `fCompl0` | 1 bit | 1 | `src/aig/gia/gia.h:L80` | Complement attribute of the first fanin; read by `Gia_ObjFaninC0` `src/aig/gia/gia.h:L545` |
| `fMark0` | 1 bit | 1 | `src/aig/gia/gia.h:L81` | First user-controlled mark, per its declaration comment; carries three further meanings, listed in section 8 |
| `fTerm` | 1 bit | 1 | `src/aig/gia/gia.h:L82` | Set only by the two terminal constructors, `Gia_ManAppendCi` `src/aig/gia/gia.h:L708` and `Gia_ManAppendCo` `src/aig/gia/gia.h:L832`; read by `Gia_ObjIsTerm` `src/aig/gia/gia.h:L510` |
| `iDiff1` | 29 bits | 2 | `src/aig/gia/gia.h:L84` | Backward offset to the second fanin for a non-terminal; the combinational-I/O index instead when `fTerm` is set, read back by `Gia_ObjCioId` `src/aig/gia/gia.h:L498` |
| `fCompl1` | 1 bit | 2 | `src/aig/gia/gia.h:L85` | Complement attribute of the second fanin; read by `Gia_ObjFaninC1` `src/aig/gia/gia.h:L546` |
| `fMark1` | 1 bit | 2 | `src/aig/gia/gia.h:L86` | Second user-controlled mark, per its declaration comment; see section 8 |
| `fPhase` | 1 bit | 2 | `src/aig/gia/gia.h:L87` | The value under the all-zero pattern, per its declaration comment; recomputed under two manager flags, see section 8 |
| `Value` | 32 bits | 3 | `src/aig/gia/gia.h:L89` | Application-specific; the rebuild protocol of section 9 uses it as the copy literal. Read and written by `Gia_ObjValue` and `Gia_ObjSetValue` `src/aig/gia/gia.h:L500-501` |

There is no type-tag field in the list, and no fanin-count field. An object's kind is inferred, as
section 7 sets out. A third fanin exists only for a real MUX and is not in the object at all: its
control literal lives in the manager's `p->pMuxes` side array, written at
`src/aig/gia/gia.h:L804` and `src/aig/gia/gia.h:L812` and read by `Gia_ObjFanin2`
`src/aig/gia/gia.h:L551`.

```mermaid
graph TD
    subgraph W1["Word 1 - bytes 0 to 3 - gia.h L79-82"]
        A0["iDiff0 : 29"]
        A1["fCompl0 : 1"]
        A2["fMark0 : 1"]
        A3["fTerm : 1"]
    end
    subgraph W2["Word 2 - bytes 4 to 7 - gia.h L84-87"]
        B0["iDiff1 : 29"]
        B1["fCompl1 : 1"]
        B2["fMark1 : 1"]
        B3["fPhase : 1"]
    end
    subgraph W3["Word 3 - bytes 8 to 11 - gia.h L89"]
        C0["Value : 32"]
    end
    W1 --> W2 --> W3
```

Diagram: the three 32-bit words of `Gia_Obj_t`. From `src/aig/gia/gia.h:L76-90`. The expanded
version, with the sentinels and the per-kind field values, is in
[`./ENCODING.md`](./ENCODING.md#2-the-three-words).

## 5. Fanin Encoding: Relative Offsets

A fanin is not an address and not an absolute identifier. It is a **backward offset**, measured in
units of `Gia_Obj_t` from the referencing object down to its fanin, and it is resolved by a single
subtraction. In the address form:

```c
static inline Gia_Obj_t *  Gia_ObjFanin0( Gia_Obj_t * pObj )                   { return pObj - pObj->iDiff0;  }
```

at `src/aig/gia/gia.h:L549`, and in the identifier form, which performs the same arithmetic on
integers:

```c
static inline int          Gia_ObjFaninId0( Gia_Obj_t * pObj, int ObjId )      { return ObjId - pObj->iDiff0;    }
```

at `src/aig/gia/gia.h:L556`. The second fanin has the matching pair, `Gia_ObjFanin1`
`src/aig/gia/gia.h:L550` and `Gia_ObjFaninId1` `src/aig/gia/gia.h:L557`. The raw fields are also
readable directly, through `Gia_ObjDiff0` and `Gia_ObjDiff1` `src/aig/gia/gia.h:L543-544`.

Three consequences follow, and they are used throughout the rest of this document.

**The object array is topologically ordered by construction.** Subtracting a non-negative offset can
only reach a lower index, so every fanin has a smaller identifier than its user. Nothing enforces
this separately — the arithmetic leaves no way to express the opposite direction. The one function
that rewrites an existing fanin offset, `Gia_ManPatchCoDriver` `src/aig/gia/gia.h:L916-922`, asserts
the same direction before writing, at `src/aig/gia/gia.h:L919`.

**The encoding survives reallocation.** Growth reallocates the array, at
`src/aig/gia/gia.h:L693`, which may move its base address. A difference is unaffected by the move,
because referrer and referent shift together; an address stored in a 29-bit field would not be, and
an absolute identifier would cost the same 29 bits while still needing an extra sentinel for "no
fanin". A second piece of the package treats the difference as the canonical storage form to be
restored: `src/aig/gia/giaFront.c` holds a frontier representation in which the offset fields
temporarily carry frontier slot numbers, and `Gia_ManFrontTransform`
`src/aig/gia/giaFront.c:L65-93` converts them back to backward offsets by subtracting an absolute
identifier from the current index, at `src/aig/gia/giaFront.c:L76` and
`src/aig/gia/giaFront.c:L81-82`.

**An observed corollary: a held `Gia_Obj_t *` does not survive growth.** The same reallocation at
`src/aig/gia/gia.h:L693` invalidates any address a caller is holding across an append. Identifiers
obtained from `Gia_ObjId` `src/aig/gia/gia.h:L497` are unaffected, which is why the identifier-form
accessors exist alongside the address-form ones throughout `src/aig/gia/gia.h:L543-571`.

```mermaid
graph RL
    OI["p->pObjs at index i - iDiff0 = 2, iDiff1 = 1"]
    OA["p->pObjs at index i-1"]
    OB["p->pObjs at index i-2"]
    OZ["p->pObjs at index 0 - the constant-0 object"]
    OI -- "Gia_ObjFanin0 subtracts iDiff0" --> OB
    OI -- "Gia_ObjFanin1 subtracts iDiff1" --> OA
    OB -.-> OZ
    OA -.-> OZ
```

Diagram: offset resolution over the flat array; both targets of the object at index *i* sit at lower
indices, so both precede it. From `src/aig/gia/gia.h:L549` and `src/aig/gia/gia.h:L556`.

The full treatment — the exact bit layout, why the fields are 29 bits wide, both sentinels, the field
values each constructor writes, and what the encoding cannot express — is in
[`./ENCODING.md`](./ENCODING.md).

## 6. The Manager

`Gia_Man_t` is declared at `src/aig/gia/gia.h:L96-259`. It is one flat object array plus a large
number of optional side tables, and the measured `sizeof(Gia_Man_t)` is 1136. Counting the
semicolon-bearing declaration lines between `src/aig/gia/gia.h:L99` and `src/aig/gia/gia.h:L258`
gives 153 fields; a naive count that also swallows the closing `};` at `src/aig/gia/gia.h:L259`
reports 154 instead. Reading it field by field is not the way in — most of the 153 belong to one
subsystem or another and are irrelevant to the representation. Grouped by role:

| Block | Lines | What it is for |
|---|---|---|
| Core storage and counts | `src/aig/gia/gia.h:L99-111` | `pName`, `pSpec`, `nRegs`, `nRegsAlloc`, `nObjs`, `nObjsAlloc`, `pObjs`, `pMuxes`, `nXors`, `nMuxes`, `nBufs`, and the identifier lists `vCis` and `vCos` |
| Structural hashing | `src/aig/gia/gia.h:L112-116` | `vHash`, `vHTable`, and the three mode flags `fAddStrash`, `fSweeper`, `fGiaSimple` |
| References and levels | `src/aig/gia/gia.h:L117-124` | `vRefs`, `pRefs`, `pLutRefs`, `vLevels`, `nLevels`, `nConstrs`, `nTravIds`, `nFront` |
| Equivalences and choices | `src/aig/gia/gia.h:L125-131` | `pReprsOld`, `pReprs`, `pNexts`, `pSibls`, `pIso`, and two ternary-state counters |
| Fanout | `src/aig/gia/gia.h:L132-135` | `pFanData` and `nFansAlloc` for dynamic fanout; `vFanoutNums` and `vFanout` for static fanout |
| Mapping, cells, packing | `src/aig/gia/gia.h:L136-149` | `vMapping`, `vMapping2`, `vFanouts2`, `vCellMapping`, `vPacking`, `vConfigs`, `pCellStr`, `vLutConfigs`, the four edge vectors |
| Counter-examples | `src/aig/gia/gia.h:L150-152` | `pCexComb`, `pCexSeq`, `vSeqModelVec` |
| Copy maps | `src/aig/gia/gia.h:L153-154` | `vCopies` and `vCopies2`, whose three access patterns are in section 8 |
| Classes and abstraction | `src/aig/gia/gia.h:L155-165` | `vVar2Obj`, `vTruths`, and the flop, gate, object, init and register class vectors |
| Switching, placement, holes | `src/aig/gia/gia.h:L166-168` | `pSwitching`, `pPlacement`, `pAigExtra` |
| Timing | `src/aig/gia/gia.h:L169-179` | Arrival and required times for inputs, outputs and combinational I/O, plus `And2Delay` and the defaults |
| Traversal identifiers | `src/aig/gia/gia.h:L180-181` | `pTravIds` and `nTravIdsAlloc`, the out-of-object mark scheme of section 7 |
| Names and user identifiers | `src/aig/gia/gia.h:L182-195` | The three name vectors, the user-assigned identifier vectors, the original-identifier vectors, `vClockDoms`, `vTiming` |
| Sub-managers and statistics | `src/aig/gia/gia.h:L196-207` | `pManTime`, `pLutLib`, `nHashHit`, `nHashMiss`, four generic user-data slots, `nAnd2Delay`, `fVerbose`, `MappedArea`, `MappedDelay` |
| Bit-parallel simulation | `src/aig/gia/gia.h:L208-222` | The block introduced by the comment at `src/aig/gia/gia.h:L208`; none of its 14 declaration lines carries a comment |
| Incremental simulation | `src/aig/gia/gia.h:L223-227` | The block introduced at `src/aig/gia/gia.h:L223`; none of its 4 declaration lines carries a comment |
| Truth tables, balancing, quantification | `src/aig/gia/gia.h:L228-245` | Small-function truth tables, the balancing scratch vectors, and the existential-quantification block |
| Retiming and box iteration | `src/aig/gia/gia.h:L246-253` | `vStopsF` and `vStopsB`; then the four box-boundary indices under the comment at `src/aig/gia/gia.h:L249` |
| ISOP and MFFC scratch | `src/aig/gia/gia.h:L254-258` | `vTTISOPs`, `vTTLut`, `vMFFCsInfo`, `vMFFCsLuts`, `vLutsRankings` |

Of the 153 declaration lines, 25 carry no comment at all. They are exactly the built-in simulation
block at `src/aig/gia/gia.h:L209-222`, the incremental simulation block at
`src/aig/gia/gia.h:L224-227`, `pUData` at `src/aig/gia/gia.h:L245`, the two retiming stop vectors at
`src/aig/gia/gia.h:L247-248`, and the four box-boundary indices at `src/aig/gia/gia.h:L250-253`.

Some of those are recoverable from their use sites and some are not, and the difference is stated
rather than papered over:

- `nSimWords` `src/aig/gia/gia.h:L211` is the per-object stride into the simulation vector.
  `Gia_ManObjSim` `src/aig/gia/giaGen.c:L106-109` is
  `return Vec_WrdEntryP( p->vSims, p->nSimWords * iObj );` at `src/aig/gia/giaGen.c:L108`, which
  fixes the meaning exactly.
- The four box-boundary indices `src/aig/gia/gia.h:L250-253` are fixed by the iterators that consume
  them, at `src/aig/gia/gia.h:L1264-1271`: `iFirstAndObj` and `iFirstPoObj` bound the object walk,
  `iFirstNonPiId` bounds the combinational-input walk, and `iFirstPoId` starts the
  combinational-output walk.
- `pUData` `src/aig/gia/gia.h:L245` **is not determinable from the source.** Its type `Gia_Dat_t` is
  forward-declared at `src/aig/gia/gia.h:L55` and defined nowhere in `src/`; a tree-wide search for
  `Gia_Dat_t` returns exactly those two lines, and a search for `pUData` returns exactly one, its own
  declaration. The field is never read, written, allocated or freed.
- `vPolars` `src/aig/gia/gia.h:L222` **has no determinable meaning inside this package.** Nothing in
  `src/aig/gia/` assigns or reads it; the only reference to it here besides the declaration is its
  release, `Vec_BitFreeP( &p->vPolars );` at `src/aig/gia/giaMan.c:L98`. It is filled from outside
  the package — see item 6 of section 13.

Eight of the fields are `Vec_Int_t` **by value** rather than by address, which is why the code that
touches them takes their address: `vHash` `src/aig/gia/gia.h:L112`, `vHTable`
`src/aig/gia/gia.h:L113`, `vRefs` `src/aig/gia/gia.h:L117`, `vCopies` `src/aig/gia/gia.h:L153`,
`vCopies2` `src/aig/gia/gia.h:L154`, `vCopiesTwo` `src/aig/gia/gia.h:L242`, `vSuppVars`
`src/aig/gia/gia.h:L243` and `vVarMap` `src/aig/gia/gia.h:L244`. So the hashing layer writes
`&p->vHTable` and `&p->vHash`, for example at `src/aig/gia/giaHash.c:L56` and
`src/aig/gia/giaHash.c:L61`, and the copy-map helpers write `&p->vCopies` at
`src/aig/gia/gia.h:L588-592`. Everything else is an address the manager owns or borrows.

Counts are derived rather than stored, in the block at `src/aig/gia/gia.h:L470-486`:

```c
static inline int          Gia_ManPiNum( Gia_Man_t * p )       { return Vec_IntSize(p->vCis) - p->nRegs;                                   }
```

at `src/aig/gia/gia.h:L472`, and

```c
static inline int          Gia_ManAndNum( Gia_Man_t * p )      { return p->nObjs - Vec_IntSize(p->vCis) - Vec_IntSize(p->vCos) - 1;        }
```

at `src/aig/gia/gia.h:L476` — the `- 1` being the constant-0 object. `Gia_ManPoNum` is the same
subtraction over `vCos` `src/aig/gia/gia.h:L473`, and `Gia_ManAndNotBufNum`
`src/aig/gia/gia.h:L480` subtracts `Gia_ManBufNum` from the AND count. Choices are likewise derived:
`Gia_ManHasChoices` is `p->pSibls != NULL` `src/aig/gia/gia.h:L484`, and `Gia_ManChoiceNum`
`src/aig/gia/gia.h:L485` counts the non-zero entries of that array. Three counts *are* stored,
because they cannot be derived: `nXors`, `nMuxes` and `nBufs` `src/aig/gia/gia.h:L107-109`,
incremented by their constructors at `src/aig/gia/gia.h:L784`, `src/aig/gia/gia.h:L814` and
`src/aig/gia/gia.h:L823`.

For ownership, the authoritative enumeration is `Gia_ManMemory`
`src/aig/gia/giaMan.c:L196-213`, which accounts for the manager struct itself, the object array, the
two identifier lists, the hash table, the reference array, and the level, cell-mapping, copy,
arrival, required and name vectors. Using it is more reliable than inferring ownership from 153
declarations.

Teardown is `Gia_ManStop` `src/aig/gia/giaMan.c:L82-183`. Counting release operations over that
range, by lines whose first token matches: 20 active `ABC_FREE` calls, one further `ABC_FREE`
commented out at `src/aig/gia/giaMan.c:L170`, and 73 calls to a `Vec_` release helper — a `Free`,
`FreeP`, `FreeFree` or `Erase` form. Counted by a different pattern the last of those three numbers
changes, so it is quoted with its method. The function ends with a run of seven
`ABC_FREE` calls at `src/aig/gia/giaMan.c:L176-182` — `pRefs`, `pLutRefs`, `pMuxes`, `pObjs`,
`pSpec`, `pName`, and then the manager struct itself — with the closing brace at
`src/aig/gia/giaMan.c:L183`. `Gia_ManStopP` `src/aig/gia/giaMan.c:L226-232` is the same teardown
through a held slot: it returns early on null, stops the manager, and nulls the slot.

```mermaid
graph TD
    MAN["Gia_Man_t"]
    OBJS["pObjs - flat Gia_Obj_t array"]
    LISTS["vCis and vCos - CI and CO object ids"]
    HTAB["vHTable - bucket heads"]
    MUX["pMuxes"]
    TRAV["pTravIds"]
    REFS["pRefs"]
    REPR["pReprs"]
    NEXT["pNexts"]
    SIBL["pSibls"]
    HASH["vHash"]
    MAN --> OBJS
    MAN --> LISTS
    MAN --> HTAB
    OBJS -- "one entry per object id" --> MUX
    OBJS --> TRAV
    OBJS --> REFS
    OBJS --> REPR
    OBJS --> NEXT
    OBJS --> SIBL
    OBJS --> HASH
    HTAB --> HASH
```

Diagram: the flat object array with the side arrays that are indexed by object identifier, and the
two identifier lists. From `src/aig/gia/gia.h:L96-259` and `src/aig/gia/giaMan.c:L196-213`.

## 7. Identity, Kinds, and Traversal

### Identity

An object's identifier is its index in the flat array, obtained by subtraction and guarded by a range
assertion:

```c
static inline int          Gia_ObjId( Gia_Man_t * p, Gia_Obj_t * pObj )        { assert( p->pObjs <= pObj && pObj < p->pObjs + p->nObjs ); return pObj - p->pObjs; }
```

at `src/aig/gia/gia.h:L497`. The reverse direction is `Gia_ManObj` `src/aig/gia/gia.h:L489`, which
asserts `v >= 0 && v < p->nObjs`, and the family around it resolves the indirect handles: object 0
through `Gia_ManConst0` `src/aig/gia/gia.h:L487`, its complement through `Gia_ManConst1`
`src/aig/gia/gia.h:L488`, and combinational I/O through `Gia_ManCi` and `Gia_ManCo`
`src/aig/gia/gia.h:L490-491`, which index the identifier lists. `Gia_ManPi`, `Gia_ManPo`,
`Gia_ManRo` and `Gia_ManRi` `src/aig/gia/gia.h:L492-495` are positional views of those same two
lists, each asserting its range.

Two conventions carry a complement alongside an object, and both are in use. A **literal** is
`id << 1 | compl`, and every object-writing constructor returns one — `Gia_ManAppendCi` ends with
`return Gia_ObjId( p, pObj ) << 1;` at `src/aig/gia/gia.h:L712`, and so do the other five. The second
convention keeps the complement in the low bit of a `Gia_Obj_t *`, through `Gia_Regular`, `Gia_Not`,
`Gia_NotCond` and `Gia_IsComplement` at `src/aig/gia/gia.h:L464-467`. The sanctioned bridge between
them is `Gia_Obj2Lit` and `Gia_Lit2Obj` at `src/aig/gia/gia.h:L525-526`; both conventions are set out
in [`./ENCODING.md`](./ENCODING.md#8-literals-complementation-and-the-two-conventions).

### Kinds

There is no type-tag field. A kind is inferred from three things only: the `fTerm` bit, the presence
of the `GIA_NONE` sentinel `src/aig/gia/gia.h:L45` in `iDiff0`, and the relative ordering of `iDiff0`
against `iDiff1`. Fourteen predicates, declared consecutively at `src/aig/gia/gia.h:L510-523`, read
that encoding back:

| Predicate | Line | What it answers |
|---|---|---|
| `Gia_ObjIsTerm` | `L510` | Is this a terminal — `fTerm` read directly |
| `Gia_ObjIsAndOrConst0` | `L511` | Is this a non-terminal — the negation of the above |
| `Gia_ObjIsCi` | `L512` | Terminal with the sentinel in `iDiff0`: a combinational input |
| `Gia_ObjIsCo` | `L513` | Terminal without it: a combinational output |
| `Gia_ObjIsAnd` | `L514` | Non-terminal with a real offset in `iDiff0` — true for plain ANDs, buffers, real XORs and real MUXes alike |
| `Gia_ObjIsXor` | `L515` | An `Gia_ObjIsAnd` whose `iDiff0` is strictly less than `iDiff1`: a real XOR |
| `Gia_ObjIsMuxId` | `L516` | Does the `p->pMuxes` side array hold a control literal for this identifier |
| `Gia_ObjIsMux` | `L517` | The same test reached from an address, via `Gia_ObjId` |
| `Gia_ObjIsAndReal` | `L518` | An `Gia_ObjIsAnd` with `iDiff0` greater than `iDiff1` that is not a MUX |
| `Gia_ObjIsBuf` | `L519` | Equal offsets, neither of them the sentinel, `fTerm` clear: a buffer |
| `Gia_ObjIsAndNotBuf` | `L520` | An `Gia_ObjIsAnd` whose offsets differ — the predicate that exists because the one at `L514` cannot exclude buffers |
| `Gia_ObjIsCand` | `L521` | An AND or a combinational input |
| `Gia_ObjIsConst0` | `L522` | Both offsets hold the sentinel |
| `Gia_ManObjIsConst0` | `L523` | This is the array's first object, tested by address |

The conditions above are stated in words; the exact expressions are on the cited lines, and
[`./ENCODING.md`](./ENCODING.md#6-offset-ordering-as-a-type-tag) tabulates them term by term. Read as
a field pattern, the seven kinds are:

| Kind | `fTerm` | `iDiff0` | `iDiff1` | Also needed | Written by |
|---|---|---|---|---|---|
| constant-0 | 0 | `GIA_NONE` | `GIA_NONE` | is object 0 | `Gia_ManStart` `src/aig/gia/giaMan.c:L64` |
| combinational input | 1 | `GIA_NONE` | CI index | — | `Gia_ManAppendCi` `src/aig/gia/gia.h:L705` |
| combinational output | 1 | offset | CO index | — | `Gia_ManAppendCo` `src/aig/gia/gia.h:L826` |
| AND | 0 | offset, with `iDiff0 >= iDiff1` | offset | not in `p->pMuxes` | `Gia_ManAppendAnd` `src/aig/gia/gia.h:L718` |
| real XOR | 0 | offset, with `iDiff0 < iDiff1` | offset | — | `Gia_ManAppendXorReal` `src/aig/gia/gia.h:L762` |
| real MUX | 0 | offset, with `iDiff0 > iDiff1` | offset | entry in `p->pMuxes` | `Gia_ManAppendMuxReal` `src/aig/gia/gia.h:L787` |
| buffer | 0 | offset | equal to `iDiff0` | — | `Gia_ManAppendBuf` `src/aig/gia/gia.h:L817` |

The consequence a maintainer meets first is a dispatch-order hazard. `Gia_ObjIsAnd`
`src/aig/gia/gia.h:L514` tests only `fTerm` and the sentinel, so it is true for buffers, real XORs
and real MUXes as well as for plain ANDs. Code that asks `Gia_ObjIsAnd` before asking
`Gia_ObjIsBuf`, `Gia_ObjIsXor` or `Gia_ObjIsMux` classifies all four as plain ANDs. The
structure-preserving branch order used in `src/aig/gia/giaDup.c` is buffer first, then within the AND
branch XOR, then MUX, then plain AND — see `src/aig/gia/giaDup.c:L1478-1488`.

```mermaid
flowchart TD
    S["An object in p->pObjs"] --> T{"fTerm set - gia.h L510"}
    T -- yes --> TC{"iDiff0 equals GIA_NONE - gia.h L512-513"}
    TC -- yes --> CI["combinational input - iDiff1 is the CI index"]
    TC -- no --> CO["combinational output - iDiff1 is the CO index"]
    T -- no --> NC{"iDiff0 equals GIA_NONE - gia.h L514, L522"}
    NC -- yes --> K0["constant-0"]
    NC -- no --> ORD{"compare iDiff0 with iDiff1"}
    ORD -- "iDiff0 equals iDiff1 - gia.h L519" --> BUF["buffer"]
    ORD -- "iDiff0 less than iDiff1 - gia.h L515" --> XOR["real XOR"]
    ORD -- "iDiff0 greater than iDiff1 - gia.h L518" --> M{"entry in p->pMuxes - gia.h L516"}
    M -- yes --> MUX["real MUX"]
    M -- no --> AND["AND"]
```

Diagram: the kind-discrimination decision tree. From `src/aig/gia/gia.h:L510-523`.

### Traversal

The header defines 62 `ForEach` iterator macros. They are conventional `for` headers, so each one
must be followed by a statement, and the filtering ones end in a negated guard followed by `{} else`, which
skips non-matching objects without needing a nested block:

```c
#define Gia_ManForEachConst( p, i )                            \
    for ( i = 1; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsConst(p, i) ) {} else
```

at `src/aig/gia/gia.h:L1102-1103`. The macros are not all in one place. The equivalence-class
iterators come first, at `src/aig/gia/gia.h:L1102-1115`, followed by the static-fanout iterators at
`src/aig/gia/gia.h:L1128-1133` and the LUT and cell iterators at `src/aig/gia/gia.h:L1160-1187`. Only
then comes the `MACRO DEFINITIONS` divider, at `src/aig/gia/gia.h:L1189-1191`, after which the main
family opens with `Gia_ManForEachObj` at `src/aig/gia/gia.h:L1193`.

| Family | Lines | Members and behaviour |
|---|---|---|
| Equivalence classes | `src/aig/gia/gia.h:L1102-1115` | `Gia_ManForEachConst`, `Gia_ManForEachClass`, `Gia_ManForEachClass0`, `Gia_ManForEachClassReverse`, and the three `Gia_ClassForEachObj` walks, which follow `Gia_ObjNext` and assert that the start is a class head |
| Static fanout | `src/aig/gia/gia.h:L1128-1133` | `Gia_ObjForEachFanoutStatic`, its identifier form, and `Gia_ObjForEachFanoutStaticIndex`, which has no callers — see item 5 of section 13 |
| LUTs and cells | `src/aig/gia/gia.h:L1160-1187` | `Gia_ManForEachLut` and its reverse; three `Gia_LutForEachFanin` walks at `src/aig/gia/gia.h:L1164-1168`; the `Lut2` variants over `vMapping2` at `src/aig/gia/gia.h:L1171-1177` with `Gia_LutForEachFanin2` and `Gia_LutForEachFanout2` at `src/aig/gia/gia.h:L1179-1181`; and `Gia_ManForEachCell` `src/aig/gia/gia.h:L1184` with `Gia_CellForEachFanin` `src/aig/gia/gia.h:L1186`, which iterate over literals rather than objects |
| All objects | `src/aig/gia/gia.h:L1193-1208` | `Gia_ManForEachObj` from index 0 and `Gia_ManForEachObj1` from index 1, plus the `ObjVec` variants that walk an identifier vector, with start, stop, reverse and literal forms |
| Reverse | `src/aig/gia/gia.h:L1209-1212` | `Gia_ManForEachObjReverse` uses `i >= 0` and therefore does reach object 0; `Gia_ManForEachObjReverse1` uses `i > 0` and does not |
| Buffers | `src/aig/gia/gia.h:L1213-1216` | `Gia_ManForEachBuf` begins at `Gia_ManBufNum(p) ? 0 : p->nObjs`, so it self-disables in one comparison when the manager holds no buffers; `Gia_ManForEachBufId` has no such guard |
| ANDs and MUXes | `src/aig/gia/gia.h:L1217-1230` | `Gia_ManForEachAnd` and its identifier form, `Gia_ManForEachMuxId`, `Gia_ManForEachCand`, the two reverse AND walks at `src/aig/gia/gia.h:L1225-1228` which use `i > 0`, and `Gia_ManForEachMux` |
| Combinational I/O | `src/aig/gia/gia.h:L1231-1250` | The `Ci` and `Co` families over the two identifier lists, their vector and reverse forms, and the two driver walks that resolve each combinational output's fanin |
| Primary I/O and flops | `src/aig/gia/gia.h:L1251-1262` | `Gia_ManForEachPi`, `Po`, `Ro`, `Ri`, the paired `Gia_ManForEachRiRo`, and `Gia_ManForEachRoToRiVec` — all positional views of `vCis` and `vCos` |
| Box-aware | `src/aig/gia/gia.h:L1264-1271` | Four macros bounded by the manager's box-boundary indices: `iFirstAndObj` and `iFirstPoObj` for the forward and reverse object walks, `iFirstNonPiId` for combinational inputs, `iFirstPoId` for combinational outputs |

The out-of-object alternative to the mark bits is a generation counter. `Gia_ManIncrementTravId`
`src/aig/gia/giaUtil.c:L190-205` allocates `p->pTravIds` lazily at `Gia_ManObjNum(p) + 100` entries
`src/aig/gia/giaUtil.c:L194-195`, doubles and zeroes the new half whenever the graph outgrows it
`src/aig/gia/giaUtil.c:L198-203`, and then increments `p->nTravIds`
`src/aig/gia/giaUtil.c:L204`. Because a mark means "this entry equals the current generation", one
increment clears every mark at once, in constant time. The twelve accessors at
`src/aig/gia/gia.h:L646-657` read and write it, each asserting that the identifier is inside
`p->nTravIdsAlloc`. Two generations are addressable at a time: `Gia_ObjSetTravIdPrevious`
`src/aig/gia/gia.h:L647` writes `p->nTravIds - 1`, so a pass can distinguish "seen this round" from
"seen last round", and the `Update` forms `src/aig/gia/gia.h:L650-651` test and set in one call.

## 8. Overloaded and Reused Fields

Twelve bytes leave no room for a field per purpose, so several fields carry more than one meaning, and
which meaning applies depends on the object's kind or on a manager flag. The table gives, for each
meaning, who writes it and what selects it.

| Field | Meaning | Who writes it | What selects this meaning | Citation |
|---|---|---|---|---|
| `iDiff1` | Backward offset to the second fanin | `Gia_ManAppendAnd` and the other non-terminal constructors | `fTerm` is clear | `src/aig/gia/gia.h:L724-736` |
| `iDiff1` | Index of this object in the manager's combinational-input or combinational-output list | `Gia_ManAppendCi`, `Gia_ManAppendCo`, each taking the index *before* pushing | `fTerm` is set | `src/aig/gia/gia.h:L710`, `src/aig/gia/gia.h:L835` |
| `iDiff1` | Read back as that index | `Gia_ObjCioId`, `Gia_ObjSetCioId`, both asserting `fTerm` | caller asserts a terminal | `src/aig/gia/gia.h:L498-499` |
| `Value` | Copy literal during a rebuild, with `~0` meaning "not copied yet" | `Gia_ManFillValue` seeds `~0`; each append's return value overwrites it | a duplication or rebuild pass is running | `src/aig/gia/giaUtil.c:L369-374`, `src/aig/gia/giaDup.c:L1751-1752` |
| `Value` | Generic application-specific scratch, per its declaration comment | any pass | no pass owns it | `src/aig/gia/gia.h:L89` |
| `Value` | Frontier slot number, cleared to 0 at the end of the transform | `Gia_ManFrontTransform` | the frontier representation is in use | `src/aig/gia/giaFront.c:L80-90` |
| `Value` | Claimed by an old note to hold the hash-table next-link — it does not | nothing | never; see item 1 of section 13 | `src/aig/gia/gia.h:L91-93` |
| `fMark0`, `fMark1` | Two independent user-controlled marks, per their declaration comments | any pass; bulk-set and bulk-cleared by the five helpers in `src/aig/gia/giaUtil.c` | no other convention is active | `src/aig/gia/gia.h:L81`, `src/aig/gia/gia.h:L86`, `src/aig/gia/giaUtil.c:L218`, `src/aig/gia/giaUtil.c:L237`, `src/aig/gia/giaUtil.c:L256`, `src/aig/gia/giaUtil.c:L294`, `src/aig/gia/giaUtil.c:L313` |
| `fMark0`, `fMark1` | One four-valued ternary-simulation state, packed as 00, 10, 01 and 11 | `Gia_ObjTerSimSetC`, `Set0`, `Set1`, `SetX`; read by the four matching getters | ternary simulation is running; the same three states are named as values by `GIA_ZER`, `GIA_ONE` and `GIA_UND` `src/aig/gia/gia.h:L924-926`, which the ternary-value helpers just below consume `src/aig/gia/gia.h:L928-944` | `src/aig/gia/gia.h:L946-949`, `src/aig/gia/gia.h:L951-954` |
| `fMark0`, `fMark1` | A two-bit saturating fanout counter on each *fanin* of a new AND | `Gia_ManAppendAnd`, as `if ( pFan0->fMark0 ) pFan0->fMark1 = 1; else pFan0->fMark0 = 1;` | `p->fSweeper` is set | `src/aig/gia/gia.h:L743-748` |
| `fMark0` | Delete marker: a marked object is skipped, and the mark is cleared as it is consumed | callers mark; `Gia_ManDupMarked` counts, skips and clears | a marked duplication is running | `src/aig/gia/giaDup.c:L1460-1461`, `src/aig/gia/giaDup.c:L1472-1476` |
| `fPhase` | Value under the all-zero pattern, per its declaration comment | `Gia_ManSetPhase` over every object; `Gia_ManCleanPhase` clears it | the default convention | `src/aig/gia/gia.h:L87`, `src/aig/gia/giaUtil.c:L420-426`, `src/aig/gia/giaUtil.c:L472-478` |
| `fPhase` | Value under a caller-supplied input pattern | `Gia_ManSetPhasePattern`, which asserts one entry per combinational input | that function has been called | `src/aig/gia/giaUtil.c:L427-437` |
| `fPhase` | Value under the all-one input pattern | `Gia_ManSetPhase1`, which sets every combinational input to 1 first | that function has been called | `src/aig/gia/giaUtil.c:L450-459` |
| `fPhase` | Conjunction of the two fanin phases, computed at construction time | `Gia_ManAppendAnd` | `p->fSweeper` is set, or `p->fBuiltInSim` is set | `src/aig/gia/gia.h:L749`, `src/aig/gia/gia.h:L755` |
| `fPhase` | Read with a tagged address's complement folded in | `Gia_ObjPhaseReal`, versus the plain `Gia_ObjPhase` | the caller holds a possibly complemented address | `src/aig/gia/gia.h:L502-503` |
| `p->pTravIds` | An out-of-object mark with generation semantics, replacing the mark bits when a pass must not disturb them | `Gia_ManIncrementTravId` and the twelve accessors | the pass chooses it | `src/aig/gia/gia.h:L646-657`, `src/aig/gia/giaUtil.c:L190-205` |

Two of those rows deserve emphasis because choosing the wrong one is silent.

**`Gia_ManFillValue` and `Gia_ManCleanValue` are not interchangeable.** The first writes `~0` into
every `Value`, at `src/aig/gia/giaUtil.c:L372-373`; the second writes 0, at
`src/aig/gia/giaUtil.c:L354-355`. The rebuild protocol tests "already copied" as `~pObj->Value`, for
example at `src/aig/gia/giaDup.c:L1751-1752`, and `~0` is the only value for which that test is
false. Seeding with 0 instead marks every object as already copied to literal 0.

**`fPhase` is a shared field with no owner.** Any of the four phase helpers can have run last, and
`Gia_ManAppendAnd` overwrites it during construction under either of two manager flags
`src/aig/gia/gia.h:L749` and `src/aig/gia/gia.h:L755`. A pass that depends on a particular phase
convention sets it itself.

### The copy maps

Besides `Value`, three further copy mechanisms coexist, and they are separate storage with separate
lifetimes. All three are `Vec_Int_t` members embedded by value, so all three helper families take the
member's address.

| Access pattern | Accessors | Index | Cleared to |
|---|---|---|---|
| Frame-indexed, for time-frame expansion | `Gia_ObjCopyF`, `Gia_ObjSetCopyF` `src/aig/gia/gia.h:L588-589` | `Gia_ManObjNum(p) * f + Gia_ObjId(p,pObj)`, so one full copy per frame `f` | no dedicated helper |
| Flat over `vCopies` | `Gia_ObjCopyArray`, `Gia_ObjSetCopyArray` `src/aig/gia/gia.h:L590-591` | the object identifier | `Gia_ManCleanCopyArray` fills with −1 `src/aig/gia/gia.h:L592` |
| Flat over `vCopies2` | `Gia_ObjCopy2Array`, `Gia_ObjSetCopy2Array` `src/aig/gia/gia.h:L594-595` | the object identifier | `Gia_ManCleanCopy2Array` fills with −1 `src/aig/gia/gia.h:L596` |

The composed forms resolve a fanin and apply its complement in one call: `Gia_ObjFanin0Copy` and
`Gia_ObjFanin1Copy` `src/aig/gia/gia.h:L584-585` over `Value`, `Gia_ObjFanin2Copy`
`src/aig/gia/gia.h:L586` for a real MUX's control input, `Gia_ObjFanin0CopyF` and
`Gia_ObjFanin1CopyF` `src/aig/gia/gia.h:L598-599` over the frame-indexed map, and
`Gia_ObjFanin0CopyArray` and `Gia_ObjFanin1CopyArray` `src/aig/gia/gia.h:L600-601` over the flat one.
`Gia_ObjCopy` `src/aig/gia/gia.h:L581` reads `Value` as a literal and returns the object it names;
`Gia_ObjLitCopy` `src/aig/gia/gia.h:L582` maps a literal to the copy of that literal. Note the
sentinel difference: the flat copy arrays are cleared
to −1, while `Value` is seeded to `~0` — a rebuild that mixes the two tests will not detect an
uncopied object.

## 9. Key Data Flows

Three flows carry most of the package's traffic: building a graph, hashing into it, and copying it.
Each is given here as an entry point, a step sequence and the hazards a caller meets.

### 9.1 Construction

Entry point `Gia_ManStart` `src/aig/gia/giaMan.c:L57-69`. It allocates the manager, allocates
`nObjsMax` objects with `ABC_CALLOC`, writes `GIA_NONE` into both offset fields of object 0
`src/aig/gia/giaMan.c:L64`, sets `nObjs` to 1 `src/aig/gia/giaMan.c:L65`, and sizes the two identifier
lists at one twentieth of the object budget `src/aig/gia/giaMan.c:L66-67`. From there the caller
appends: combinational inputs, then internal objects, then combinational outputs, then
`Gia_ManSetRegNum` `src/aig/gia/giaMan.c:L766-770`. The calling discipline — which inputs and outputs
must be created in which order — is spelled out in the client recipe at
[`../../../readmeaig`](../../../readmeaig), lines 26 to 47; section 10 of this document states the
structural invariant that discipline produces.

Every constructor routes its storage request through one function. `Gia_ManAppendObj`
`src/aig/gia/gia.h:L682-704` is the single growth point of the package, and it does six things in
order:

| Step | Behavior | Line |
|---|---|---|
| Capacity test | acts only when `p->nObjs == p->nObjsAlloc` | `src/aig/gia/gia.h:L684` |
| New size | `Abc_MinInt( 2 * p->nObjsAlloc, (1 << 29) )` — doubling, capped | `src/aig/gia/gia.h:L686` |
| Hard limit | at `1 << 29` objects it prints `Hard limit on the number of nodes (2^29) is reached. Quitting...` and calls `exit(1)` | `src/aig/gia/gia.h:L687-688` |
| Regrow | `ABC_REALLOC` the object array, then `memset` the grown tail to zero | `src/aig/gia/gia.h:L693-694` |
| Third-fanin array | `p->pMuxes`, when allocated, is reallocated and zeroed in lockstep | `src/aig/gia/gia.h:L695-699` |
| Hash bookkeeping | `if ( Vec_IntSize(&p->vHTable) ) Vec_IntPush( &p->vHash, 0 );` keeps the next-link array one entry per object, then `return Gia_ManObj( p, p->nObjs++ );` | `src/aig/gia/gia.h:L702-703` |

Sixteen constructors sit on top of it, at `src/aig/gia/gia.h:L705-914`. Counted by definition,
`gia.h` holds seventeen `static inline` functions whose names begin `Gia_ManAppend`: the growth helper
plus sixteen constructors. Of those sixteen, exactly six write an object's fields directly —
`Gia_ManAppendCi` `src/aig/gia/gia.h:L705`, `Gia_ManAppendAnd` `src/aig/gia/gia.h:L718`,
`Gia_ManAppendXorReal` `src/aig/gia/gia.h:L762`, `Gia_ManAppendMuxReal` `src/aig/gia/gia.h:L787`,
`Gia_ManAppendBuf` `src/aig/gia/gia.h:L817` and `Gia_ManAppendCo` `src/aig/gia/gia.h:L826`. The other
ten compose those six: `Or` `src/aig/gia/gia.h:L841`, `Mux` `src/aig/gia/gia.h:L845`, `Maj`
`src/aig/gia/gia.h:L851`, `Xor` `src/aig/gia/gia.h:L858`, and the five `*2` forms `And2`
`src/aig/gia/gia.h:L863`, `Or2` `src/aig/gia/gia.h:L878`, `Mux2` `src/aig/gia/gia.h:L882`, `Maj2`
`src/aig/gia/gia.h:L888`, `Xor2` `src/aig/gia/gia.h:L895`, plus `XorReal2`
`src/aig/gia/gia.h:L900`. Which fields each of the six writes, and the resulting offset ordering, is
tabulated in [`./ENCODING.md`](./ENCODING.md#5-encoding-every-object-kind).

Hazards in this flow:

- `Gia_ManAppendAnd` performs no constant folding and no deduplication. It asserts distinct fanins
  unless `p->fGiaSimple` is set `src/aig/gia/gia.h:L723`, and then writes the object. Folding lives in
  the `*2` variants `src/aig/gia/gia.h:L863-877` and `src/aig/gia/gia.h:L900-914`, and in the hashing
  entry points described next. The two differ on one point: a `*2` variant folds only when
  `p->fGiaSimple` is clear, since its whole fold block sits inside `if ( !p->fGiaSimple )`
  `src/aig/gia/gia.h:L865`, whereas `Gia_ManHashAnd` folds first and tests the flag afterwards
  `src/aig/gia/giaHash.c:L578-590`.
- Five manager flags change what `Gia_ManAppendAnd` does beyond writing the object: `p->pFanData`
  adds fanout records `src/aig/gia/gia.h:L738-742`, `p->fSweeper` bumps fanin marks and computes
  `fPhase` `src/aig/gia/gia.h:L743-750`, `p->fBuiltInSim` computes `fPhase` and runs
  `Gia_ManBuiltInSimPerform` `src/aig/gia/gia.h:L751-757`, `p->vSuppWords` runs
  `Gia_ManQuantSetSuppAnd` `src/aig/gia/gia.h:L758-759`, and `p->fGiaSimple` relaxes the fanin
  assertion `src/aig/gia/gia.h:L723`.
- Every constructor returns a literal, not an address: `return Gia_ObjId( p, pObj ) << 1;`
  `src/aig/gia/gia.h:L760`. A caller that keeps addresses across appends is holding storage that
  `ABC_REALLOC` may move `src/aig/gia/gia.h:L693`.

### 9.2 Structural hashing

The hash table is optional and separately started. `Gia_ManHashAlloc`
`src/aig/gia/giaHash.c:L105-112` fills `p->vHTable` with a prime number of zeroed buckets and sizes
`p->vHash` to one entry per object. `Gia_ManHashStart` `src/aig/gia/giaHash.c:L125-136` calls the
allocator and then inserts every existing AND, so it can be run on a graph that already exists.
`Gia_ManHashStop` `src/aig/gia/giaHash.c:L149-153` erases both vectors.

The key mixes both fanin literals and, for a real MUX, the control literal, with distinct odd
multipliers per component `src/aig/gia/giaHash.c:L45-53`:

```c
    unsigned Key = iLitC * 2011;
    Key += Abc_Lit2Var(iLit0) * 7937;
    Key += Abc_Lit2Var(iLit1) * 2971;
```

Non-MUX callers pass `iLitC` as −1, for example at `src/aig/gia/giaHash.c:L602`. The two complement
bits enter the key separately, at `src/aig/gia/giaHash.c:L50-51`, and the result is reduced modulo the
table size `src/aig/gia/giaHash.c:L52`.

**Where the chain actually lives.** `Gia_ManHashFind` `src/aig/gia/giaHash.c:L54-68` returns an
`int *` — the address of the slot that either holds the matching object identifier or is the zero
slot at the end of the chain. Bucket heads are entries of `p->vHTable`
`src/aig/gia/giaHash.c:L56`; the next link of object `iThis` is entry `iThis` of `p->vHash`
`src/aig/gia/giaHash.c:L61`; zero terminates. The function asserts one `vHash` entry per object
`src/aig/gia/giaHash.c:L57`, which is the invariant `Gia_ManAppendObj` maintains at
`src/aig/gia/gia.h:L702`. No part of the chain is stored in the object.

```mermaid
graph LR
    HT["p->vHTable entry k = head object id"]
    O5["object id 5"]
    O9["object id 9"]
    O0["0 terminates the chain"]
    HT -->|"Gia_ManHashFind L56"| O5
    O5 -->|"p->vHash entry 5"| O9
    O9 -->|"p->vHash entry 9"| O0
```

Diagram: bucket heads in `p->vHTable`, next links in `p->vHash` indexed by object identifier.
From `src/aig/gia/giaHash.c:L54-68`.

`Gia_ManHashAnd` `src/aig/gia/giaHash.c:L576-620` is the flow a caller normally uses.

```mermaid
flowchart TD
    A["Gia_ManHashAnd iLit0 iLit1"] --> B{"constant literal or equal or complementary"}
    B -->|"yes L578-585"| C["return folded literal"]
    B -->|"no"| D{"p->fGiaSimple"}
    D -->|"yes L586-590"| E["assert empty vHTable then Gia_ManAppendAnd"]
    D -->|"no"| F{"every 256th object and table under half the AND count"}
    F -->|"yes L591-592"| G["Gia_ManHashResize"]
    F -->|"no"| H{"p->fAddStrash"}
    G --> H
    H -->|"yes L593-595"| I{"Gia_ManAddStrash returned an object"}
    H -->|"no"| J["swap so iLit0 is less than iLit1 L599-600"]
    I -->|"yes L596-597"| C
    I -->|"no"| J
    J --> K["pPlace = Gia_ManHashFind L602"]
    K --> L{"slot non zero"}
    L -->|"hit L603-607"| M["nHashHit++ then return Abc_Var2Lit"]
    L -->|"miss L608"| N{"vHash has spare capacity L609"}
    N -->|"yes"| O["write through pPlace after Gia_ManAppendAnd L610"]
    N -->|"no"| P["append then re-run Gia_ManHashFind L613-616"]
    O --> Q["return Abc_Var2Lit L618"]
    P --> Q
```

Diagram: structural-hashing lookup and insert. From `src/aig/gia/giaHash.c:L45-68` and
`src/aig/gia/giaHash.c:L576-620`.

**The interior-slot hazard, and why there are three guards.** The `int * pPlace` that
`Gia_ManHashFind` returns points inside `p->vHash` or `p->vHTable`. Appending an object pushes onto
`p->vHash` `src/aig/gia/gia.h:L702`, which can reallocate that vector and invalidate the slot
address. Each constructor that inserts therefore tests spare capacity first and, when there is none,
appends and then re-runs the lookup: `Gia_ManHashXorReal` at `src/aig/gia/giaHash.c:L497`,
`Gia_ManHashMuxReal` at `src/aig/gia/giaHash.c:L552`, and `Gia_ManHashAnd` at
`src/aig/gia/giaHash.c:L609`, all reading `if ( Vec_IntSize(&p->vHash) < Vec_IntCap(&p->vHash) )`.
This is a distinct hazard from the object-array reallocation at `src/aig/gia/gia.h:L693`: one
invalidates an `int *` into a vector, the other invalidates a `Gia_Obj_t *` into the object array.

**Canonicalization differs per operator, and the hashing layer compares literals in all three
cases.** `Gia_ManHashAnd` swaps when `iLit0 > iLit1` `src/aig/gia/giaHash.c:L599`.
`Gia_ManHashXorReal` swaps when `iLit0 < iLit1` `src/aig/gia/giaHash.c:L483`, then strips both
complements and re-applies them to the returned literal `src/aig/gia/giaHash.c:L485-488`.
`Gia_ManHashMuxReal` swaps the two data literals when `iLit0 > iLit1` and negates the control literal
with them `src/aig/gia/giaHash.c:L540-541`, then normalizes on `Abc_LitIsCompl(iLit1)`
`src/aig/gia/giaHash.c:L542-543`. The append layer below compares differently: `Gia_ManAppendAnd`
compares literals `src/aig/gia/gia.h:L724`, while `Gia_ManAppendXorReal`
`src/aig/gia/gia.h:L770` and `Gia_ManAppendMuxReal` `src/aig/gia/gia.h:L798` compare variables. The
two layers are not interchangeable on this point.

Further behavior in this flow:

- `Gia_ManHashAnd` folds the four constant cases at `src/aig/gia/giaHash.c:L578-585` *before* it
  reaches the `p->fGiaSimple` branch at `src/aig/gia/giaHash.c:L586-590`, so folding happens under
  that flag too.
- `Gia_ManHashAndTry` `src/aig/gia/giaHash.c:L637-655` folds and looks up but never creates: on a
  miss it returns −1 `src/aig/gia/giaHash.c:L653`.
- `Gia_ManHashResize` `src/aig/gia/giaHash.c:L166-194` re-buckets into a table of
  `Abc_PrimeCudd( 2 * Gia_ManAndNum(p) )` entries `src/aig/gia/giaHash.c:L173`, then checks its own
  work: it counts what it moved and asserts that count equals
  `Gia_ManAndNum(p) - Gia_ManBufNum(p)` `src/aig/gia/giaHash.c:L189-190`.
- `Gia_ManRehash` `src/aig/gia/giaHash.c:L739-769` rebuilds a whole graph through the table: start a
  new manager, allocate its table, seed constant 0, dispatch every object, stop the table, set the
  register count, then run the result through `Gia_ManCleanup` using the swap idiom
  `pNew = Gia_ManCleanup( pTemp = pNew );` then `Gia_ManStop( pTemp );`
  `src/aig/gia/giaHash.c:L766-767`. Its buffer branch is commented out
  `src/aig/gia/giaHash.c:L752-754`; the consequence is recorded as item 8 of section 13.

### 9.3 Duplication and rebuild

Copying is the package's normal way of transforming a graph: a pass reads an old manager and writes a
new one. `src/aig/gia/giaDup.c` holds ninety functions matching
`grep -cE '^Gia_Man_t \* Gia_ManDup' src/aig/gia/giaDup.c`, that is ninety definitions that begin at
column one and return a new manager. Counted differently the number differs, so the count is quoted
with its command.

All of them share one protocol, carried by `Value`:

1. Seed constant 0 with literal 0 — `Gia_ManConst0(p)->Value = 0;`
   `src/aig/gia/giaDup.c:L730`.
2. Walk the source objects and dispatch on kind.
3. Store each append's return literal into the **source** object's `Value`, so the old object records
   where its copy landed.
4. Resolve a fanin through `Gia_ObjFanin0Copy` and `Gia_ObjFanin1Copy`
   `src/aig/gia/gia.h:L584-585`, which read the fanin's `Value` and apply the edge's complement.

Straight-line variants rely on iteration order — `Gia_ManDup` walks with `Gia_ManForEachObj1`
`src/aig/gia/giaDup.c:L731`, so every fanin has been copied before it is read, and no sentinel is
needed. Variants that skip objects or recurse call `Gia_ManFillValue` first: `Gia_ManDupDfs` at
`src/aig/gia/giaDup.c:L1766` uses `~0` as its visited test — `if ( ~pObj->Value )` guarding an early
`return;` at `src/aig/gia/giaDup.c:L1751-1752` — and `Gia_ManDupMarked` at
`src/aig/gia/giaDup.c:L1462` leaves the `Value` of every skipped object at `~0`.

```mermaid
sequenceDiagram
    participant Pass as transformation pass
    participant Old as source manager p
    participant New as new manager pNew
    Pass->>Old: Gia_ManFillValue writes ~0 into every Value - skipping and recursing variants only
    Pass->>New: Gia_ManStart sizes the new object array
    Pass->>Old: Gia_ManConst0 Value set to literal 0
    loop each source object in order
        Pass->>Old: read kind and fanins
        Pass->>Old: Gia_ObjFanin0Copy reads the fanin Value
        Pass->>New: Gia_ManAppendAnd or Ci or Co or Buf
        New-->>Pass: literal of the new object
        Pass->>Old: store that literal into this object Value
    end
    Pass->>New: Gia_ManSetRegNum once
```

Diagram: the `Value` copy protocol used by the duplication family. From `src/aig/gia/giaDup.c:L720-750`,
`src/aig/gia/giaDup.c:L1454-1502` and `src/aig/gia/giaUtil.c:L369-374`.

**Two entry points define the family's semantics, and they are not equivalent.**

`Gia_ManDup` `src/aig/gia/giaDup.c:L720-750` dispatches buffer, then AND, then combinational input,
then combinational output `src/aig/gia/giaDup.c:L733-744`, and carries choices across by allocating
and filling `pNew->pSibls` `src/aig/gia/giaDup.c:L728-729` and `src/aig/gia/giaDup.c:L738-739`. What
it does not have is a real-XOR branch or a real-MUX branch, and it never allocates `pNew->pMuxes`. A
real XOR therefore matches `Gia_ObjIsAnd` and is recreated by `Gia_ManAppendAnd`, whose ordering
normalization `if ( iLit0 < iLit1 )` `src/aig/gia/gia.h:L724` writes `iDiff0 >= iDiff1` — the
`iDiff0 < iDiff1` pattern that marked it a real XOR is gone.

`Gia_ManDupMarked` `src/aig/gia/giaDup.c:L1454-1502` is the structure-preserving variant. It counts
marked objects `src/aig/gia/giaDup.c:L1460-1461`, calls `Gia_ManFillValue`
`src/aig/gia/giaDup.c:L1462`, sizes the new manager as `Gia_ManObjNum(p) - CountMarked`
`src/aig/gia/giaDup.c:L1463`, allocates `pNew->pMuxes` when the source has one
`src/aig/gia/giaDup.c:L1464-1465`, and dispatches buffer, then AND split into real XOR, real MUX and
plain AND, then combinational input, then combinational output
`src/aig/gia/giaDup.c:L1478-1499`. It treats `fMark0` as a delete marker, asserting that no buffer is
marked and clearing the mark as it skips `src/aig/gia/giaDup.c:L1472-1476`, and it finishes by
asserting that the new manager is exactly full `src/aig/gia/giaDup.c:L1501`.

Two smaller entry points show the same protocol used to merge one graph into another.
`Gia_ManDupAppend` `src/aig/gia/giaDup.c:L1147-1165` starts the destination's hash table if it is not
already live `src/aig/gia/giaDup.c:L1153-1154` yet appends its ANDs raw with `Gia_ManAppendAnd`
`src/aig/gia/giaDup.c:L1159`, and optionally reuses the destination's combinational inputs instead of
creating new ones `src/aig/gia/giaDup.c:L1161`. `Gia_ManDupAppendShare`
`src/aig/gia/giaDup.c:L1166-1183` requires equal combinational-input counts
`src/aig/gia/giaDup.c:L1170` and goes through `Gia_ManHashAnd` instead
`src/aig/gia/giaDup.c:L1177`.

**Ownership.** A duplicating call returns a new manager and leaves the input alone; freeing the input
is the caller's business, which is what the `pTemp` swap idiom at `src/aig/gia/giaHash.c:L766-767`
does. The clearest case is sweeping:

```c
Gia_Man_t * Gia_ManCleanup( Gia_Man_t * p )
{
    Gia_ManCombMarkUsed( p );
    return Gia_ManDupMarked( p );
}
```

`src/aig/gia/giaScl.c:L84-88`. It marks the unused objects and returns a marked duplicate; the
original is still allocated when it returns. The client recipe states the same thing at
`readmeaig:L43`, where `Gia_ManCleanup()` is described as returning a new AIG.

### 9.4 Manager lifecycle

```mermaid
stateDiagram-v2
    state "manager allocated, object 0 present" as Empty
    state "appending, no hash table" as Building
    state "appending, hash table live" as Hashed
    state "register count fixed" as Sealed
    state "copy returned by a Dup entry point" as Copied
    [*] --> Empty : Gia_ManStart giaMan.c L57
    Empty --> Building : Gia_ManAppendCi and friends
    Empty --> Hashed : Gia_ManHashStart giaHash.c L125
    Building --> Hashed : Gia_ManHashStart giaHash.c L125
    Hashed --> Building : Gia_ManHashStop giaHash.c L149
    Building --> Sealed : Gia_ManSetRegNum giaMan.c L766 once only
    Hashed --> Sealed : Gia_ManSetRegNum giaMan.c L766 once only
    Sealed --> Copied : Gia_ManCleanup giaScl.c L84
    Copied --> [*] : Gia_ManStop frees each manager giaMan.c L82
    Sealed --> [*] : Gia_ManStop giaMan.c L82
```

Diagram: the states a manager passes through. From `src/aig/gia/giaMan.c:L57`,
`src/aig/gia/giaMan.c:L82`, `src/aig/gia/giaMan.c:L766`, `src/aig/gia/giaHash.c:L125`,
`src/aig/gia/giaHash.c:L149` and `src/aig/gia/giaScl.c:L84-88`.

Two paths through construction coexist in the repository, and they differ in exactly one respect. The
client recipe at [`../../../readmeaig`](../../../readmeaig) builds through the hashing layer, calling
`Gia_ManHashAnd` at `readmeaig:L37`. The GoogleTest cases at
[`../../../test/gia/gia_test.cc`](../../../test/gia/gia_test.cc) build through the raw append layer
and never start a hash table. Both are complete construction sequences; the hashing path
deduplicates and folds, the raw path stores exactly what it is told.

## 10. Invariants

These hold of every well-formed manager. Many are enforced by an assertion, and the assertion is
cited where one exists.

1. **Every fanin offset reaches backward.** Both accessors subtract, so a fanin is always at a lower
   index than the object that names it, and the object array is therefore in topological order by
   construction `src/aig/gia/gia.h:L549` and `src/aig/gia/gia.h:L556`. The one function that rewires
   an existing edge asserts it: `assert( Gia_ObjId(p, pObjCo) > Abc_Lit2Var(iLit0) );`
   `src/aig/gia/gia.h:L919`.
2. **Object 0 is the constant-0 object.** `Gia_ManStart` writes `GIA_NONE` into both of its offset
   fields `src/aig/gia/giaMan.c:L64` and sets the object count to 1
   `src/aig/gia/giaMan.c:L65`, so `Gia_ManObjIsConst0` can test identity by address
   `src/aig/gia/gia.h:L523`.
3. **An AND object has `iDiff0 >= iDiff1`.** `Gia_ManAppendAnd` assigns the larger offset to `iDiff0`
   in both branches of its literal comparison `src/aig/gia/gia.h:L724-736`.
4. **A real XOR object has `iDiff0 < iDiff1`.** `Gia_ManAppendXorReal` deliberately writes the
   opposite ordering `src/aig/gia/gia.h:L770-783`, which is what marks the object as a real XOR.
5. **A real MUX object has `iDiff0 > iDiff1` and requires `p->pMuxes`.**
   `Gia_ManAppendMuxReal` asserts the array exists `src/aig/gia/gia.h:L790` before writing the two
   data offsets and the control literal `src/aig/gia/gia.h:L798-813`.
6. **A buffer has `iDiff0 == iDiff1`,** written in one statement
   `pObj->iDiff0  = pObj->iDiff1  = Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit);`
   `src/aig/gia/gia.h:L821`, with the complement bits likewise equal
   `src/aig/gia/gia.h:L822`.
7. **A terminal has `fTerm` set,** and the two terminal kinds are separated by `iDiff0`: a
   combinational input has `GIA_NONE` there `src/aig/gia/gia.h:L708-709`, a combinational output has a
   real offset `src/aig/gia/gia.h:L832-833`, which is exactly what the two predicates test
   `src/aig/gia/gia.h:L512-513`.
8. **For a terminal, `iDiff1` is its index in the manager's identifier list.** Each terminal
   constructor takes the list size *before* pushing, so index and position agree —
   `src/aig/gia/gia.h:L710-711` for inputs and `src/aig/gia/gia.h:L835-836` for outputs — and
   `Gia_ObjCioId` reads it back under an assertion that the object is a terminal
   `src/aig/gia/gia.h:L498-499`.
9. **An object address handed to `Gia_ObjId` lies inside the live array,** asserted as
   `p->pObjs <= pObj && pObj < p->pObjs + p->nObjs` `src/aig/gia/gia.h:L497`.
10. **`p->nObjs` never exceeds `p->nObjsAlloc`, and neither exceeds `1 << 29`.** The growth path runs
    only on equality, caps the new size, and refuses to continue at the ceiling
    `src/aig/gia/gia.h:L684-701`.
11. **While the hash table is live, `p->vHash` holds exactly one entry per object.**
    `Gia_ManHashFind` asserts `Vec_IntSize(&p->vHash) == Gia_ManObjNum(p)`
    `src/aig/gia/giaHash.c:L57`, and `Gia_ManAppendObj` maintains it by pushing one zero per new
    object `src/aig/gia/gia.h:L702`.
12. **A resized table holds every live AND except buffers.** `Gia_ManHashResize` counts what it
    re-buckets and asserts that count equals `Gia_ManAndNum(p) - Gia_ManBufNum(p)`
    `src/aig/gia/giaHash.c:L189-190`.
13. **`p->nRegs` is assigned once per manager.** `Gia_ManSetRegNum` asserts it is still zero
    `src/aig/gia/giaMan.c:L768` before assigning.
14. **The identifier lists are partitioned by position, not by a flag.** All primary inputs precede
    all flop outputs in `p->vCis`, and all primary outputs precede all flop inputs in `p->vCos`,
    which is what lets `Gia_ManPiNum` be a subtraction `src/aig/gia/gia.h:L472` and lets `Gia_ManRo`
    and `Gia_ManRi` reach a flop by adding the primary count to the index —
    `return Gia_ManCi( p, Gia_ManPiNum(p)+v );` `src/aig/gia/gia.h:L494-495`. The calling discipline
    that produces this ordering is stated in the client recipe, at `readmeaig:L35` —
    "(It is important to create all PIs first, before creating flop outputs)." — and at
    `readmeaig:L41` — "(it is important to create all POs first, before creating register inputs)." —
    where *register inputs* is that document's wording for what this one calls flop inputs.
15. **Every traversal-identifier accessor stays inside the side array,** each asserting
    `Gia_ObjId(p, pObj) < p->nTravIdsAlloc` `src/aig/gia/gia.h:L646-657`; `Gia_ManIncrementTravId`
    is what keeps that true, by allocating and then doubling the array
    `src/aig/gia/giaUtil.c:L190-205`.
16. **`p->pMuxes`, when allocated, has the same length as the object array,** reallocated and zeroed
    inside the same growth step `src/aig/gia/gia.h:L695-699`.
17. **Static fanout is self-indexing.** The first entries of `p->vFanout` are offsets into the same
    vector: `Gia_ObjFoffsetId` reads the offset `src/aig/gia/gia.h:L1118` and `Gia_ObjFanoutId` adds
    the fanout number to it `src/aig/gia/gia.h:L1122`.
18. **Equivalence classes key on `GIA_VOID`, never on `GIA_NONE`.** A class head has representative
    `GIA_VOID` and a positive next link `src/aig/gia/gia.h:L1093`; an unclassified object has
    `GIA_VOID` and no next link `src/aig/gia/gia.h:L1094`; and undoing a pair writes `GIA_VOID`
    back `src/aig/gia/gia.h:L1100`.

## 11. Gotchas

Each entry below is a place where correct-looking code is wrong.

1. **`Gia_ObjIsAnd` is true for buffers, real XORs and real MUXes.** It tests only that the object is
   not a terminal and has a real first offset `src/aig/gia/gia.h:L514`, so a dispatch chain that
   tests it first will never reach the more specific kinds. `Gia_ObjIsAndNotBuf`
   `src/aig/gia/gia.h:L520` exists for that reason, and the working order is the one
   `Gia_ManDupMarked` uses: buffer, then AND split into real XOR, real MUX and plain
   `src/aig/gia/giaDup.c:L1478-1488`.
2. **There are two "none" sentinels, of two widths, differing by one hex digit.** `GIA_NONE` is
   `0x1FFFFFFF` and belongs to the 29-bit offset fields; `GIA_VOID` is `0x0FFFFFFF` and belongs to
   the 28-bit representative field `src/aig/gia/gia.h:L45-46`. Class code tests `GIA_VOID`
   `src/aig/gia/gia.h:L1093-1100`; offset code tests `GIA_NONE` `src/aig/gia/gia.h:L510-523`.
3. **A held `Gia_Obj_t *` does not survive an append that grows the array.** `ABC_REALLOC` may move
   the storage `src/aig/gia/gia.h:L693`. Object identifiers and literals survive; addresses do not.
4. **A held `int *` into the hash vectors does not survive an append either.** Appending pushes onto
   `p->vHash` `src/aig/gia/gia.h:L702`, so the slot address `Gia_ManHashFind` returned may be stale;
   the three insert paths guard against it at `src/aig/gia/giaHash.c:L497`,
   `src/aig/gia/giaHash.c:L552` and `src/aig/gia/giaHash.c:L609`. This is a second, separate hazard
   from the one above: there an object address into `p->pObjs` goes stale, here an `int *` into
   `p->vHash` does.
5. **`Gia_ManAppendAnd` does not fold constants.** Passing it a constant literal creates an AND with
   a constant fanin. The folding forms are `Gia_ManAppendAnd2` `src/aig/gia/gia.h:L863-877` and its
   siblings, and the hashing entry points `src/aig/gia/giaHash.c:L578-585`.
6. **`Gia_ManDup` and `Gia_ManDupMarked` are not interchangeable, and their banners do not say so.**
   `Gia_ManDup` `src/aig/gia/giaDup.c:L720` has no real-XOR and no real-MUX branch, so those objects
   are rebuilt as plain ANDs and lose their encoding; `Gia_ManDupMarked`
   `src/aig/gia/giaDup.c:L1454` preserves them. Both banners carry the identical `Synopsis` text
   `[Duplicates AIG without any changes.]`, at `src/aig/gia/giaDup.c:L711` and
   `src/aig/gia/giaDup.c:L1445`, so the difference is not visible from the banners.
7. **`Gia_ManCleanup` returns a new manager and does not free its input**
   `src/aig/gia/giaScl.c:L84-88`. The caller owns two managers after the call.
8. **`Gia_ManFillValue` and `Gia_ManCleanValue` are not interchangeable.** `~0`
   `src/aig/gia/giaUtil.c:L372-373` is the sentinel the rebuild test `if ( ~pObj->Value )` looks for
   `src/aig/gia/giaDup.c:L1751-1752`; `0` `src/aig/gia/giaUtil.c:L354-355` reads as a valid copy
   literal.
9. **`Gia_ManHashAndTry` returns −1 on a miss** `src/aig/gia/giaHash.c:L653` and creates nothing,
   unlike every other `Gia_ManHash*` constructor. A caller that treats the result as a literal will
   use −1 as one.
10. **A rehash silently removes buffers.** The buffer branch of `Gia_ManRehash` is commented out
    `src/aig/gia/giaHash.c:L752-754`, so a buffer falls through to `Gia_ObjIsAnd`
    `src/aig/gia/giaHash.c:L755` and is rebuilt as `Gia_ManHashAnd` of one literal with itself, which
    folds to that literal `src/aig/gia/giaHash.c:L582-583`.
11. **`Gia_ManSetRegNum` may be called only once per manager,** because it asserts the count is still
    zero `src/aig/gia/giaMan.c:L768`. A second call aborts rather than updating.
12. **`Gia_ObjFanin2` and `Gia_ObjFaninC2` do not check that the object is a MUX.** Both test only
    that `p->pMuxes` is non-null `src/aig/gia/gia.h:L551` and `src/aig/gia/gia.h:L547`, so on a plain
    AND in a manager that has the array they read that object's entry, which is zero, and report
    object 0 with no complement.
13. **Under `p->fGiaSimple`, an AND of a literal with itself is written as a buffer.** The distinct-
    fanin assertion is relaxed `src/aig/gia/gia.h:L723`, and equal fanins produce
    `iDiff0 == iDiff1`, which is precisely what `Gia_ObjIsBuf` tests
    `src/aig/gia/gia.h:L519`.
14. **Two manager flags make `Gia_ManAppendAnd` write fields the caller did not ask it to write.**
    Under `p->fSweeper` it bumps each fanin's mark bits as a two-bit counter and overwrites the new
    object's `fPhase` `src/aig/gia/gia.h:L743-750`; under `p->fBuiltInSim` it overwrites `fPhase` and
    calls `Gia_ManBuiltInSimPerform` `src/aig/gia/gia.h:L751-757`.
15. **`Gia_ManForEachBuf` self-disables.** Its start index is `Gia_ManBufNum(p) ? 0 : p->nObjs`
    `src/aig/gia/gia.h:L1213-1214`, so on a manager with no buffers the loop body never runs — which
    is the intent, but it also means the macro cannot be used to discover buffers the counter does
    not know about.
16. **The reverse iterators are not uniform about object 0.** `Gia_ManForEachObjReverse` bounds with
    `i >= 0` and does visit it `src/aig/gia/gia.h:L1209-1210`, while `Gia_ManForEachObjReverse1`
    `src/aig/gia/gia.h:L1211-1212`, `Gia_ManForEachAndReverse` `src/aig/gia/gia.h:L1225-1226` and
    `Gia_ManForEachAndReverseId` `src/aig/gia/gia.h:L1227-1228` bound with `i > 0` and do not.
17. **A filtering iterator must be followed by a statement, and cannot be followed by an `else`.**
    The macros end in a negated guard followed by `{} else`, as in
    `if ( !Gia_ObjIsConst(p, i) ) {} else` `src/aig/gia/gia.h:L1103`, so the loop body
    binds to that dangling `else`.
18. **`Gia_ObjIsXor` takes only the object, but `Gia_ObjIsMux` needs the manager.** Real XOR is
    readable from the object's own fields `src/aig/gia/gia.h:L515`, while MUX-ness lives in the
    manager's side array `src/aig/gia/gia.h:L516-517`. Code holding only an object address cannot
    answer the second question.
19. **Four copy mechanisms coexist:** `Value`, the frame-indexed map, the flat `vCopies` map and the
    flat `vCopies2` map `src/aig/gia/gia.h:L588-596`. They are separate storage, and their empty
    markers differ — the arrays are filled with −1, `Value` with `~0`.
20. **Two fanin helpers behave oddly at the edges.** `Gia_ObjFaninNum` reports 3, 2, 1 or 0 by kind
    and reports 2 for a buffer `src/aig/gia/gia.h:L573`; `Gia_ObjWhatFanin` ends in
    `assert(0); return -1;` `src/aig/gia/gia.h:L574`, so asking it about a non-fanin aborts a debug
    build and yields −1 otherwise.

## 12. Limitations

**A manager holds at most 2^29 objects, and the limit is fatal rather than reported.** The growth
path caps the new size at `1 << 29` `src/aig/gia/gia.h:L686` and, once the count reaches that value,
ends the process `src/aig/gia/gia.h:L688`:

```c
            printf( "Hard limit on the number of nodes (2^29) is reached. Quitting...\n" ), exit(1);
```

There is no return code and no error path for a caller to handle. The limit follows from the field
width: an offset has 29 bits `src/aig/gia/gia.h:L79`, and `GIA_NONE` `src/aig/gia/gia.h:L45` consumes
the largest of the values those bits can hold.

**Equivalence classes address at most 2^28 − 1 representatives.** `Gia_Rpr_t::iRepr` is 28 bits wide
`src/aig/gia/gia.h:L60`, with `GIA_VOID` `src/aig/gia/gia.h:L46` occupying its top value. The
representative space is therefore half the object space, so a manager can hold objects that no
representative field can name.

**A graph is a whole; edits are copies.** The offsets are backward, so inserting an object between two
existing ones is not expressible without rewriting the offsets that cross it, and the constructors
only ever append `src/aig/gia/gia.h:L703`. One function rewires an existing edge in place,
`Gia_ManPatchCoDriver` `src/aig/gia/gia.h:L916-922`, and it is restricted to a combinational output's
single driver and asserts the backward direction. Everything else transforms by building a new manager
and returning it, which is what makes the ninety duplication entry points in
`src/aig/gia/giaDup.c` the package's main working surface and what makes `Gia_ManCleanup`
`src/aig/gia/giaScl.c:L84-88` return a manager rather than modify one.

**A manager is not shareable across threads while a pass is running.** The central header declares no
lock, no atomic and no thread-local storage: `src/aig/gia/gia.h` contains no reference to `pthread` or
to a mutex. Meanwhile a pass writes into the representation itself — the mark bits
`src/aig/gia/gia.h:L81` and `src/aig/gia/gia.h:L86`, `fPhase` `src/aig/gia/gia.h:L87`, `Value`
`src/aig/gia/gia.h:L89`, the traversal counter `src/aig/gia/giaUtil.c:L190-205`, and the two hash
vectors `src/aig/gia/gia.h:L702`. Those are the same fields section 8 shows to be shared between
passes.

## 13. Observed Discrepancies and Dead Code

The twelve items below were found while writing this document. They are recorded as observations only:
**nothing in this list was changed, fixed, removed or worked around**, and each entry says so.

1. **A stale comment about `Value`.** The note at `src/aig/gia/gia.h:L91-93` states that `Value`
   holds the next node in the hash table during structural hashing. The chain is not in the object:
   bucket heads are in `p->vHTable` and next links are in `p->vHash`, indexed by object identifier
   `src/aig/gia/giaHash.c:L54-68`, which is why `Gia_ManAppendObj` pushes one `vHash` entry per new
   object `src/aig/gia/gia.h:L702`. The comment text was not edited.
2. **A C23 keyword in a C translation unit.** `Gia_ManEvalCutHashing`
   `src/aig/gia/giaCut.c:L1078` contains `Vec_Int_t vTemp = {0, 0, nullptr};`
   `src/aig/gia/giaCut.c:L1081`. `nullptr` is a keyword in C23 and not in earlier C, and the makefile
   pins no standard for the C sources — `-std=c++17` is set for C++ only, at `Makefile:L158` — so
   whether this translation unit compiles depends on the compiler's default dialect. Compiled with
   `-std=gnu17` it fails with `'nullptr' undeclared (first use in this function)`; compiled by a
   toolchain whose default is C23 it succeeds. It is the only occurrence of `nullptr` in the package's
   123 `.c` files. The file was not edited.
3. **A header that declares nothing.** `src/aig/gia/giaIiff.h:L1-54` is 54 lines of include guard
   and namespace scaffolding with no declaration, no definition and no include. It was not deleted.
4. **A commented-out release inside the destructor.** `Gia_ManStop` carries
   `//    ABC_FREE( p->pMapping );` at `src/aig/gia/giaMan.c:L170`, between two live releases. The
   line was left byte-identical.
5. **A macro with no callers.** `Gia_ObjForEachFanoutStaticIndex`
   `src/aig/gia/gia.h:L1132-1133` is used nowhere in `src/`; a tree-wide search returns one hit, its
   own definition. Its loop guard is `&& (Index = Vec_IntEntry(p->vFanout, Id)+i) &&`, a conjunct that
   is false when the computed index is 0, which ends the iteration. The macro was not changed or
   removed.
6. **A manager field that is written only from outside the package and never read.**
   `Vec_Bit_t *    vPolars;` `src/aig/gia/gia.h:L222` is released by
   `Vec_BitFreeP( &p->vPolars );` `src/aig/gia/giaMan.c:L98`. Nothing inside `src/aig/gia/` assigns
   or reads it. Outside the package it is allocated as
   `pNew->vPolars = Vec_BitStart( Gia_ManObjNum(pNew) );` `src/base/acb/acbFunc.c:L509` and written at
   `src/base/acb/acbFunc.c:L518`, on a manager created by `Gia_ManStart( 10000 )`
   `src/base/acb/acbFunc.c:L487` inside `Gia_FileSimpleParse` `src/base/acb/acbFunc.c:L443`; no site
   in `src/` reads it back. Two similarly named fields are unrelated to this one — they belong to
   different structures *and* have a different type, `Vec_Int_t *`: `Seg_Man_t::vPolars`
   `src/aig/gia/giaSatEdge.c:L50`, assigned at `src/aig/gia/giaSatEdge.c:L159`, and
   `Sle_Man_t::vPolars` `src/aig/gia/giaSatLE.c:L431`, assigned at
   `src/aig/gia/giaSatLE.c:L465`. Nothing was changed.
7. **A type that is declared and never defined, and a field of that type that is never used.**
   `typedef struct Gia_Dat_t_            Gia_Dat_t;` `src/aig/gia/gia.h:L55` has no definition
   anywhere in `src/`, and `Gia_Dat_t *    pUData;` `src/aig/gia/gia.h:L245` is never read, written,
   allocated or freed. A tree-wide search for the type name returns exactly those two lines. Neither
   was removed.
8. **Dead code with a behavioral consequence.** The buffer branch of `Gia_ManRehash` is commented out
   at `src/aig/gia/giaHash.c:L752-754`. Because `Gia_ObjIsAnd` is true for a buffer, a buffer reaches
   the AND branch `src/aig/gia/giaHash.c:L755-756`, where both fanin copies are the same literal, and
   `Gia_ManHashAnd` folds equal arguments to that argument `src/aig/gia/giaHash.c:L582-583`. A rehash
   therefore removes buffers without reporting it. Nothing was changed.
9. **Two commented-out assertions.** `Gia_ManAppendXorReal` `src/aig/gia/gia.h:L762` carries
   `//assert( !Abc_LitIsCompl(iLit0) );` and `//assert( !Abc_LitIsCompl(iLit1) );` at
   `src/aig/gia/gia.h:L768-769`. The corresponding canonicalization is done by the caller
   `src/aig/gia/giaHash.c:L485-488`. The lines were left as they are.
10. **Commented-out lines naming fields that no longer exist.** `Gia_ObjRecognizeMuxTwo`
    `src/aig/gia/giaHash.c:L241-312` carries several commented fragments referring to `p1` and `p2`,
    at `src/aig/gia/giaHash.c:L248-275`, for example `//        if ( FrGia_IsComplement(pNode1->p2) )`
    at `src/aig/gia/giaHash.c:L248`. `Gia_Obj_t` has no such members
    `src/aig/gia/gia.h:L76-90`. The comments were not removed.
11. **An encoding ambiguity that one flag makes reachable.** With `p->fGiaSimple` set, the
    distinct-fanin assertion in `Gia_ManAppendAnd` is relaxed `src/aig/gia/gia.h:L723`, so an AND of a
    literal with itself is stored with `iDiff0 == iDiff1` — the pattern `Gia_ObjIsBuf` reports as a
    buffer `src/aig/gia/gia.h:L519`. Nothing was changed.
12. **Context only: an empty scaffold exists in a sibling package.** `src/aig/aig/aig_.c:L1-53` is
    53 lines of banner, includes and section dividers with no function. It belongs to `src/aig/aig`,
    which is outside this package; it is recorded here only as context for the observation that the
    codebase contains placeholder files. It was not touched.

## 14. Where to Look Next

| Destination | What is there |
|---|---|
| [`./ENCODING.md`](./ENCODING.md) | The bit-level deep-dive: the three words, why deltas, why 29 bits, the per-kind encoding table, offset ordering as a type tag, terminal overload, the two complement conventions, a worked example, and a source index |
| [`../../../readmeaig`](../../../readmeaig) | The client recipe for using this package from outside, lines 26 to 47 — the call sequence, in the author's own words |
| [`../../../test/gia/gia_test.cc`](../../../test/gia/gia_test.cc) | The only executable GIA example in the repository: 54 lines, four `TEST(GiaTest, ...)` cases at lines 7, 14, 22 and 32, wrapped in `ABC_NAMESPACE_IMPL_START` at line 5 and `ABC_NAMESPACE_IMPL_END` at line 54 |
| [`./gia.h`](./gia.h) | The package's own table of contents: the `/*===` group markers from `src/aig/gia/gia.h:L1277` to `src/aig/gia/gia.h:L1837`, which say which file implements what |
| [`./giaMan.c`](./giaMan.c), [`./giaHash.c`](./giaHash.c), [`./giaDup.c`](./giaDup.c), [`./giaUtil.c`](./giaUtil.c) | The four implementation files this document draws on, in the reading order given in section 3 |

This description was written against branch `master` at commit `17cadca08`. Every claim above cites
the file and line range it came from, so a reader who finds the code has moved on can re-check any
single statement against the source rather than the description.
