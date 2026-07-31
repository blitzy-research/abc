/**CFile****************************************************************

  FileName    [gia.h]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [External declarations.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: gia.h,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/
 
#ifndef ABC__aig__gia__gia_h
#define ABC__aig__gia__gia_h


////////////////////////////////////////////////////////////////////////
///                          INCLUDES                                ///
////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "misc/vec/vec.h"
#include "misc/vec/vecWec.h"
#include "misc/util/utilCex.h"

////////////////////////////////////////////////////////////////////////
///                         PARAMETERS                               ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_HEADER_START

// Two "none" sentinels, of two different widths, for two different purposes.
//
// GIA_NONE is 0x1FFFFFFF = 536870911 = 2^29-1, which is exactly the largest value a 29-bit field
// can hold.  That is what lets it serve as the "no fanin" marker for the two 29-bit offset fields
// of Gia_Obj_t without costing a separate flag bit and without giving up more than the single
// largest representable offset.  The offset fields are the fields that key on it: Gia_ManStart
// writes it into both offsets of the constant-0 object (giaMan.c:L64), Gia_ManAppendCi writes it
// into iDiff0, and five of the kind predicates below test against it.
//
// GIA_VOID is 0x0FFFFFFF = 268435455 = 2^28-1, which is exactly the largest value a *28*-bit field
// can hold, and it belongs to a different struct: the 28-bit Gia_Rpr_t::iRepr declared just below.
// The equivalence-class helpers near the end of this header key on GIA_VOID and never on GIA_NONE.
//
// The two differ by one hex digit and are not interchangeable.  An offset field set to GIA_VOID is
// an ordinary offset of 268435455, not a "none"; a 28-bit iRepr cannot hold GIA_NONE at all.
#define GIA_NONE 0x1FFFFFFF
#define GIA_VOID 0x0FFFFFFF

////////////////////////////////////////////////////////////////////////
///                         BASIC TYPES                              ///
////////////////////////////////////////////////////////////////////////

// Opaque forward declarations.  The three memory-manager types are defined in giaMem.c; client code
// only ever holds addresses of them.  Gia_Dat_t is different: its definition does not exist anywhere
// in src/ at this commit, and the only other reference to the name in the whole tree is the pUData
// member of Gia_Man_t below, which is itself never read, written, allocated or freed.  What Gia_Dat_t
// was meant to hold is therefore not determinable from the source; both lines are left as found.
typedef struct Gia_MmFixed_t_        Gia_MmFixed_t;    
typedef struct Gia_MmFlex_t_         Gia_MmFlex_t;     
typedef struct Gia_MmStep_t_         Gia_MmStep_t;     
typedef struct Gia_Dat_t_            Gia_Dat_t;

// One equivalence-class record per object, held in the manager's pReprs side array and indexed by
// object identifier.  The whole record is packed into a single 32-bit word: 28 + 1 + 1 + 1 + 1 bits,
// which a compiled probe confirms as sizeof(Gia_Rpr_t) == 4.  Two consequences of the 28-bit iRepr
// are worth holding on to.  First, its "none" value is GIA_VOID (2^28-1) and not GIA_NONE, because
// GIA_NONE does not fit in 28 bits.  Second, the representative space is half the object space, so a
// manager may legally hold objects whose identifiers no iRepr can name.  Gia_ObjSetRepr, near the end
// of this header, additionally asserts that a representative either is GIA_VOID or has a strictly
// smaller identifier than the object it represents - the same backward discipline the fanin offsets
// follow.
typedef struct Gia_Rpr_t_ Gia_Rpr_t;
struct Gia_Rpr_t_
{
    unsigned       iRepr   : 28;  // representative node
    unsigned       fProved :  1;  // marks the proved equivalence
    unsigned       fFailed :  1;  // marks the failed equivalence
    unsigned       fColorA :  1;  // marks cone of A
    unsigned       fColorB :  1;  // marks cone of B
};

// One placement record per object, held in the manager's pPlacement side array.  Like Gia_Rpr_t it is
// packed into a single 32-bit word - 1 + 15 + 1 + 15 bits, measured as sizeof(Gia_Plc_t) == 4 - which
// is what bounds each coordinate to the range of an unsigned 15-bit field.  Unrelated to either
// sentinel above.
typedef struct Gia_Plc_t_ Gia_Plc_t;
struct Gia_Plc_t_
{
    unsigned       fFixed  :  1;  // the placement of this object is fixed
    unsigned       xCoord  : 15;  // x-ooordinate of the placement
    unsigned       fUndef  :  1;  // the placement of this object is not assigned
    unsigned       yCoord  : 15;  // y-ooordinate of the placement
};

// The GIA object: one node of the And-Inverter Graph, and the reason this package calls itself
// scalable.  Every object in the graph - the constant, every combinational input and output, every
// AND, real XOR, real MUX and buffer - is this same struct, and nothing else.  See ./ENCODING.md for
// the bit-by-bit treatment and ./README.md section 8 for the field-reuse matrix.
//
// THREE 32-BIT WORDS, TWELVE BYTES, NO TYPE TAG, NO PADDING.  The members below are grouped into
// three words by the two blank lines: 29+1+1+1 bits, then 29+1+1+1 bits, then one full-width
// unsigned.  Both bit-field words are therefore exactly full, and a compiled probe reports
// sizeof(Gia_Obj_t) == 12 with nothing left over.  That budget is what makes every width here
// load-bearing rather than arbitrary, and it is why there is no room for a field saying what kind of
// node this is: kind is inferred instead, from fTerm, from the GIA_NONE sentinel and from the
// relative ordering of the two offsets, by the predicates further down this header.
//
// Field by field:
//
// iDiff0, iDiff1 - a BACKWARD RELATIVE OFFSET to a fanin, measured in units of Gia_Obj_t.  Not an
//     address and not an absolute identifier: Gia_ObjFanin0 resolves it by subtraction, as
//     "pObj - pObj->iDiff0", and the identifier form Gia_ObjFaninId0 performs the same arithmetic on
//     integers, as "ObjId - pObj->iDiff0".  A delta rather than an address because the object array
//     is reallocated as it grows (see Gia_ManAppendObj), and relocating the array shifts referrer and
//     referent alike, leaving their difference unchanged - an address would name a location the
//     object no longer occupies.  Because the offsets are subtracted and the fields are unsigned, a
//     fanin necessarily sits at a lower index than the object naming it, which is what makes
//     topological order an invariant of the encoding rather than a convention.  29 bits is the width
//     the object array needs: growth is capped at 1 << 29 objects, and only 3 bits remain in the same
//     word for the flags beside it.  The boundary value GIA_NONE, the largest a 29-bit field can
//     hold, means "no fanin", and is what makes the constant-0 object and every combinational input
//     recognisable without a type tag.  iDiff1 additionally carries a second, unrelated meaning:
//     when fTerm is set it is not an offset at all but the index of this object in the manager's
//     combinational-input or combinational-output list - see Gia_ObjCioId below.
// fCompl0, fCompl1 - one complement attribute per fanin, one bit each, written from the incoming
//     literal's low bit by every constructor.  Offset and complement bit together reconstitute a
//     fanin literal (Gia_ObjFaninLit0) or a complemented address (Gia_ObjChild0).  A real MUX's third
//     input has no complement bit here: it travels inside the control literal held in p->pMuxes.
// fMark0, fMark1 - general-purpose marks, and the pair carries four documented meanings depending on
//     which pass owns it: two independent user marks, as the declaration comments say; one
//     four-valued ternary-simulation state packed across both bits by the Gia_ObjTerSim* accessors
//     below; a two-bit saturating fanout counter written on the *fanins* of a new AND when
//     p->fSweeper is set; and, as fMark0 alone, a delete marker that Gia_ManDupMarked consumes and
//     clears (giaDup.c:L1472-1476).  Reading either bit therefore requires knowing which pass last
//     wrote it, which is information the twelve bytes do not carry.
// fTerm - the one genuine type bit in the object.  Set by exactly two constructors, Gia_ManAppendCi
//     and Gia_ManAppendCo; clear on the constant-0 object and on every internal node.  It is the
//     first discriminator in the kind predicates, and it is also what licenses reading iDiff1 as a
//     combinational-I/O index.
// fPhase - nominally this node's value under the all-zero input pattern, as the declaration comment
//     says.  Also written at construction time as the conjunction of the two incoming edge values -
//     each fanin's phase XORed with that edge's own complement bit - under p->fSweeper and again
//     under p->fBuiltInSim, and recomputed wholesale by the four phase helpers in giaUtil.c, so no
//     single pass owns it.
// Value - a full 32-bit word of application scratch, and the field the duplication and rebuild
//     family uses: it holds this object's copy literal in the destination manager, with ~0 as the
//     "not copied yet" sentinel that Gia_ManFillValue seeds (giaUtil.c:L369-374) and that the rebuild
//     code tests as "if ( ~pObj->Value )" (giaDup.c:L1751-1752).  Gia_ManCleanValue writes 0 instead
//     (giaUtil.c:L351-356), which is a valid copy literal and not that sentinel; the two are not
//     interchangeable.
typedef struct Gia_Obj_t_ Gia_Obj_t;
struct Gia_Obj_t_
{
    // word 1 of 3: a 29-bit offset and its three flag bits, 32 bits exactly
    unsigned       iDiff0 :  29;  // the diff of the first fanin
    unsigned       fCompl0:   1;  // the complemented attribute
    unsigned       fMark0 :   1;  // first user-controlled mark
    unsigned       fTerm  :   1;  // terminal node (CI/CO)

    // word 2 of 3: the second offset and its three flag bits, 32 bits exactly
    unsigned       iDiff1 :  29;  // the diff of the second fanin
    unsigned       fCompl1:   1;  // the complemented attribute
    unsigned       fMark1 :   1;  // second user-controlled mark
    unsigned       fPhase :   1;  // value under 000 pattern

    // word 3 of 3: one full-width unsigned, 32 bits, no bit fields
    unsigned       Value;         // application-specific value
};
// Value is currently used to store several types of information
// - pointer to the next node in the hash table during structural hashing
// - pointer to the node copy during duplication 

// The note above is retained exactly as found.  On its first bullet: at this commit the
// structural-hashing chain is not kept in Value.  Bucket heads live in p->vHTable and the chain links
// live in p->vHash, an integer vector indexed by object identifier, which is what Gia_ManHashFind
// walks (giaHash.c:L54-68) and what Gia_ManAppendObj below maintains by pushing one p->vHash entry per
// new object.  Its second bullet does describe the current code, with one refinement: what Value
// carries during duplication is a copy *literal* rather than an address, which is why Gia_ObjCopy
// applies Abc_Lit2Var to it.  Recorded in ./README.md section 13 as well.

// The manager: one flat array of objects, plus a large number of optional side tables, most of them
// indexed by object identifier and therefore kept the same length as the object array.  A compiled
// probe reports sizeof(Gia_Man_t) == 1136, but that is the fixed overhead of the struct alone - almost
// every table below is reached through an address, so a live manager's real footprint is dominated by
// heap allocations this figure does not include, the object array first among them at twelve bytes per
// object.
//
// Reading this declaration field by field is not the way in: of the 153 declaration lines, most belong
// to one subsystem or another and are irrelevant to the representation itself.  The block comments
// below mark the functional groups; ./README.md section 6 tabulates all of them with line ranges.
//
// Two practical notes.  Eight members are Vec_Int_t BY VALUE rather than by address - vHash, vHTable,
// vRefs, vCopies, vCopies2, vCopiesTwo, vSuppVars and vVarMap - which is why the code that touches
// them takes the member's address (&p->vHTable, &p->vCopies) and why teardown erases rather than frees
// them.  And for ownership, read allocation and teardown rather than the estimate: Gia_ManStart
// (giaMan.c:L57-69) shows what a manager is given at birth and Gia_ManStop (giaMan.c:L82-183) is the
// definitive list of what it releases.  Gia_ManMemory (giaMan.c:L196-213) is the package's own size
// estimate and sums fourteen terms only, so several allocations the manager owns are absent from it.
// new AIG manager
typedef struct Gia_Man_t_ Gia_Man_t;
struct Gia_Man_t_
{
    // core storage: the object array, the counters the constructors maintain instead of recomputing
    // them by scanning, and the two combinational-I/O identifier lists.  vCis and vCos are partitioned
    // by position and not by a flag: every primary input precedes every flop output in vCis, and every
    // primary output precedes every flop input in vCos, which is what lets Gia_ManPiNum be a
    // subtraction.
    char *         pName;         // name of the AIG
    char *         pSpec;         // name of the input file
    int            nRegs;         // number of registers
    int            nRegsAlloc;    // number of allocated registers
    int            nObjs;         // number of objects
    int            nObjsAlloc;    // number of allocated objects
    Gia_Obj_t *    pObjs;         // the array of objects
    unsigned *     pMuxes;        // control signals of MUXes
    int            nXors;         // the number of XORs
    int            nMuxes;        // the number of MUXes 
    int            nBufs;         // the number of buffers
    Vec_Int_t *    vCis;          // the vector of CIs (PIs + LOs)
    Vec_Int_t *    vCos;          // the vector of COs (POs + LIs)
    Vec_Int_t      vHash;         // hash links
    Vec_Int_t      vHTable;       // hash table
    int            fAddStrash;    // performs additional structural hashing
    int            fSweeper;      // sweeper is running
    int            fGiaSimple;    // simple mode (no const-propagation and strashing)
    Vec_Int_t      vRefs;         // the reference count
    int *          pRefs;         // the reference count
    int *          pLutRefs;      // the reference count
    Vec_Int_t *    vLevels;       // levels of the nodes
    int            nLevels;       // the mamixum level
    int            nConstrs;      // the number of constraints
    int            nTravIds;      // the current traversal ID
    int            nFront;        // frontier size 
    int *          pReprsOld;     // representatives (for CIs and ANDs)
    Gia_Rpr_t *    pReprs;        // representatives (for CIs and ANDs)
    int *          pNexts;        // next nodes in the equivalence classes
    int *          pSibls;        // next nodes in the choice nodes
    int *          pIso;          // pairs of structurally isomorphic nodes
    int            nTerLoop;      // the state where loop begins  
    int            nTerStates;    // the total number of ternary states
    int *          pFanData;      // the database to store fanout information
    int            nFansAlloc;    // the size of fanout representation
    Vec_Int_t *    vFanoutNums;   // static fanout
    Vec_Int_t *    vFanout;       // static fanout
    Vec_Int_t *    vMapping;      // mapping for each node
    Vec_Wec_t *    vMapping2;     // mapping for each node
    Vec_Wec_t *    vFanouts2;     // mapping fanouts 
    Vec_Int_t *    vCellMapping;  // mapping for each node
    void *         pSatlutWinman; // windowing for SAT-based mapping
    Vec_Int_t *    vPacking;      // packing information
    Vec_Int_t *    vConfigs;      // cell configurations
    Vec_Str_t *    vConfigs2;     // cell configurations
    char *         pCellStr;      // cell description
    Vec_Int_t *    vLutConfigs;   // LUT configurations
    Vec_Int_t *    vEdgeDelay;    // special edge information
    Vec_Int_t *    vEdgeDelayR;   // special edge information
    Vec_Int_t *    vEdge1;        // special edge information
    Vec_Int_t *    vEdge2;        // special edge information
    Abc_Cex_t *    pCexComb;      // combinational counter-example
    Abc_Cex_t *    pCexSeq;       // sequential counter-example
    Vec_Ptr_t *    vSeqModelVec;  // sequential counter-examples
    Vec_Int_t      vCopies;       // intermediate copies
    Vec_Int_t      vCopies2;      // intermediate copies
    Vec_Int_t *    vVar2Obj;      // mapping of variables into objects
    Vec_Int_t *    vTruths;       // used for truth table computation
    Vec_Int_t *    vFlopClasses;  // classes of flops for retiming/merging/etc
    Vec_Int_t *    vGateClasses;  // classes of gates for abstraction
    Vec_Int_t *    vObjClasses;   // classes of objects for abstraction
    Vec_Int_t *    vInitClasses;  // classes of flops for retiming/merging/etc
    Vec_Int_t *    vRegClasses;   // classes of registers for sequential synthesis
    Vec_Int_t *    vRegInits;     // initial state
    Vec_Int_t *    vDoms;         // dominators
    Vec_Int_t *    vBarBufs;      // barrier buffers
    Vec_Int_t *    vXors;         // temporary XORs
    unsigned char* pSwitching;    // switching activity for each object
    Gia_Plc_t *    pPlacement;    // placement of the objects
    Gia_Man_t *    pAigExtra;     // combinational logic of holes
    Vec_Flt_t *    vInArrs;       // PI arrival times
    Vec_Flt_t *    vOutReqs;      // PO required times
    Vec_Int_t *    vCiArrs;       // CI arrival times
    Vec_Int_t *    vCoReqs;       // CO required times
    Vec_Int_t *    vCoArrs;       // CO arrival times
    Vec_Int_t *    vCoAttrs;      // CO attributes
    Vec_Int_t *    vWeights;      // object attributes
    int            And2Delay;     // delay of the AND gate 
    float          DefInArrs;     // default PI arrival times
    float          DefOutReqs;    // default PO required times
    Vec_Int_t *    vSwitching;    // switching activity
    int *          pTravIds;      // separate traversal ID representation
    int            nTravIdsAlloc; // the number of trav IDs allocated
    Vec_Ptr_t *    vNamesIn;      // the input names 
    Vec_Ptr_t *    vNamesOut;     // the output names
    Vec_Ptr_t *    vNamesNode;    // the node names
    Vec_Int_t *    vUserPiIds;    // numbers assigned to PIs by the user
    Vec_Int_t *    vUserPoIds;    // numbers assigned to POs by the user
    Vec_Int_t *    vUserFfIds;    // numbers assigned to FFs by the user
    Vec_Int_t *    vCiNumsOrig;   // original CI names
    Vec_Int_t *    vCoNumsOrig;   // original CO names
    Vec_Int_t *    vIdsOrig;      // original object IDs
    Vec_Int_t *    vIdsEquiv;     // original object IDs proved equivalent
    Vec_Int_t *    vEquLitIds;    // original object IDs proved equivalent
    Vec_Int_t *    vCofVars;      // cofactoring variables
    Vec_Vec_t *    vClockDoms;    // clock domains
    Vec_Flt_t *    vTiming;       // arrival/required/slack
    void *         pManTime;      // the timing manager
    void *         pLutLib;       // LUT library
    word           nHashHit;      // hash table hit
    word           nHashMiss;     // hash table miss
    void *         pData;         // various user data
    unsigned *     pData2;        // various user data
    int            iData;         // various user data
    int            iData2;        // various user data
    int            nAnd2Delay;    // AND2 delay scaled to match delay numbers used
    int            fVerbose;      // verbose reports
    int            MappedArea;    // area after mapping
    int            MappedDelay;   // delay after mapping
    // bit-parallel simulation
    // Simulation state the manager owns itself instead of leaving to a caller-side table.
    // Gia_ManBuiltInSimStart (giaSim.c:L776-796) switches the block on: it sets fBuiltInSim,
    // fixes nSimWords, and allocates vSimsPi with one block of nSimWords 64-bit words per
    // combinational input and vSims with one block per object; Gia_ManStop releases the
    // vectors (giaMan.c:L95-103). Capacity is therefore 64 * nSimWords patterns, the bound
    // iPatsPi is measured against (giaSim.c:L966, L981); once it is reached and nSimWords has
    // grown to nSimWordsMax, further patterns reuse older slots in the wrapping order kept by
    // iPastPiMax (giaSim.c:L972-977). Two strides for vSimsPi coexist in the tree:
    // Gia_ObjSimWords below divides Vec_WrdSize(vSimsPi) by Gia_ManPiNum, while
    // Gia_ManBuiltInDataPi indexes by nSimWords (giaSim.c:L766).
    // Five members below are declared and released but never filled anywhere in src/ at this
    // commit, so what they were meant to hold is not determinable from the source here.
    // nSimWordsT has no reference at all beyond its declaration. vSimsT, vClassOld, vClassNew
    // and vPats are only released, by Gia_ManStop (giaMan.c:L95-100); the identically named
    // names used elsewhere belong to other structures or are locals (Gia_RsbMan_t at
    // giaSimBase.c:L1726, Hcd_Man_t at giaGiarf.c:L42-43, Supp_Man_t at giaSupps.c:L51,
    // a local at giaPat2.c:L666). vPolars is filled from outside the package
    // (base/acb/acbFunc.c:L509, L518) and released here (giaMan.c:L98), but nothing in src/ reads it.
    int            fBuiltInSim;   // built-in simulation is on
    int            iPatsPi;       // next stimulus pattern slot
    int            nSimWords;     // words of stimulus per object
    int            nSimWordsT;    // declared only; see the note above
    int            iPastPiMax;    // wrapping slot used once full
    int            nSimWordsMax;  // ceiling on nSimWords
    Vec_Wrd_t *    vSims;         // object values, nSimWords per object
    Vec_Wrd_t *    vSimsT;        // released only; never set in src/
    Vec_Wrd_t *    vSimsPi;       // input stimulus, one block per input
    Vec_Wrd_t *    vSimsPo;       // output values, one block per output
    Vec_Int_t *    vClassOld;     // released only; never set in src/
    Vec_Int_t *    vClassNew;     // released only; never set in src/
    Vec_Int_t *    vPats;         // released only; never set in src/
    Vec_Bit_t *    vPolars;       // set outside GIA; never read in src/
    // incremental simulation
    // A second simulation mode that reuses iPatsPi, nSimWords and vSims from the block above
    // and adds a generation counter. Gia_ManIncrSimStart (giaSim.c:L1150-1162) sets fIncrSim,
    // seeds iTimeStamp to 1 and allocates vTimeStamps. Gia_ManIncrSimUpdate
    // (giaSim.c:L1131-1148) extends vTimeStamps to one entry per object, extends vSims, seeds
    // every combinational input from iNextPi up to Gia_ManCiNum, then sets iNextPi to that
    // count. Gia_ManIncrSimSet (giaSim.c:L1174-1190) increments iTimeStamp, stamps each object
    // it writes, and advances iPatsPi with wraparound at 64 * nSimWords. Comparing an object's
    // stamp against iTimeStamp is what tells a pass whether that object's words are current -
    // the same generation-counter device the traversal identifiers use further down this file.
    int            fIncrSim;      // incremental simulation is on
    int            iNextPi;       // first input not yet seeded
    int            iTimeStamp;    // current generation counter
    Vec_Int_t *    vTimeStamps;   // per-object generation of last write
    // truth table computation for small functions
    int            nTtVars;       // truth table variables
    int            nTtWords;      // truth table words
    Vec_Int_t *    vTtNums;       // object numbers
    Vec_Int_t *    vTtNodes;      // internal nodes
    Vec_Ptr_t *    vTtInputs;     // truth tables for constant and primary inputs
    Vec_Wrd_t *    vTtMemory;     // truth tables for internal nodes
    // balancing
    Vec_Int_t *    vSuper;        // supergate
    Vec_Int_t *    vStore;        // node storage  
    // existential quantification
    int            iSuppPi;       // the number of support variables
    int            nSuppWords;    // the number of support words
    Vec_Wrd_t *    vSuppWords;    // support information
    Vec_Int_t      vCopiesTwo;    // intermediate copies
    Vec_Int_t      vSuppVars;     // used variables
    Vec_Int_t      vVarMap;       // used variables
    // What this member was meant to carry is not determinable from the source at this commit:
    // its type Gia_Dat_t is forward-declared at the top of this file and defined nowhere in
    // src/, and this declaration is the only other reference to either name in the tree - the
    // field is never read, written, allocated or released, not even by Gia_ManStop.
    Gia_Dat_t *    pUData;        // purpose not determinable in src/
    // retiming data
    // One stop character per object, produced by Gia_ManRetimableF and Gia_ManRetimableB on a
    // manager read from a MiniAIG (giaMini.c:L1378-1381, which first asserts both are absent)
    // and released by Gia_ManStop (giaMan.c:L161-162). The retiming feasibility check is the
    // reader: a nonzero vStopsF entry pins that object's arrival time to zero
    // (giaSif.c:L500-502), and a nonzero vStopsB entry whose time exceeds the target period
    // rejects the period (giaSif.c:L516-519). Both are guarded by a null test at each use.
    Vec_Str_t *    vStopsF;       // per-object forward stop flags
    Vec_Str_t *    vStopsB;       // per-object backward stop flags
    // iteration with boxes
    // Four boundary indices that let the WithBoxes iterators near the end of this file walk
    // only the part of the network that lies outside the boxes. The LUT mapper is the one
    // place in src/ that fills them (giaNf.c:L2721-2724): iFirstNonPiId becomes the number of
    // true primary inputs - Tim_ManPiNum when a timing manager is present, Gia_ManCiNum when
    // it is not - iFirstPoId the combinational-output index at which the true primary outputs
    // begin, iFirstAndObj one past the constant object and those true primary inputs, and
    // iFirstPoObj the object identifier of the first true primary output. Gia_ManStart
    // allocates the manager with ABC_CALLOC (giaMan.c:L61), so on a manager that has not been
    // through that pass all four read zero: the two object-walking iterators then traverse
    // nothing, Gia_ManForEachCiIdWithBoxes traverses nothing, and Gia_ManForEachCoWithBoxes
    // traverses every combinational output because its upper bound is the vCos size.
    int            iFirstNonPiId; // number of true primary inputs
    int            iFirstPoId;    // CO index where true POs begin
    int            iFirstAndObj;  // first object id after const and PIs
    int            iFirstPoObj;   // object id of the first true PO
    Vec_Str_t *    vTTISOPs;      // truth tables from ISOP computation
    Vec_Int_t *    vTTLut;      // truth tables from ISOP computation
    Vec_Int_t *    vMFFCsInfo;    // MFFC information
    Vec_Int_t *    vMFFCsLuts;        // MFFCs for each lut
    Vec_Ptr_t *    vLutsRankings;     // LUTs rankings of inputs
};


typedef struct Gps_Par_t_ Gps_Par_t;
struct Gps_Par_t_
{
    int            fTents;
    int            fSwitch;
    int            fCut;
    int            fNpn;
    int            fLutProf;
    int            fMuxXor;
    int            fMiter;
    int            fSkipMap;
    int            fSlacks;
    int            fNoColor;
    int            fMapOutStats;
    char *         pDumpFile;
};

typedef struct Emb_Par_t_ Emb_Par_t;
struct Emb_Par_t_
{
    int            nDims;         // the number of dimension
    int            nSols;         // the number of solutions (typically, 2)
    int            nIters;        // the number of iterations of FORCE
    int            fRefine;       // use refinement by FORCE
    int            fCluster;      // use clustered representation 
    int            fDump;         // dump Gnuplot file
    int            fDumpLarge;    // dump Gnuplot file for large benchmarks
    int            fShowImage;    // shows image if Gnuplot is installed
    int            fVerbose;      // verbose flag  
};


// frames parameters
typedef struct Gia_ParFra_t_ Gia_ParFra_t;
struct Gia_ParFra_t_
{
    int            nFrames;       // the number of frames to unroll
    int            fInit;         // initialize the timeframes
    int            fSaveLastLit;  // adds POs for outputs of each frame
    int            fDisableSt;    // disables strashing
    int            fOrPos;        // ORs respective POs in each timeframe
    int            fVerbose;      // enables verbose output
};


// simulation parameters
typedef struct Gia_ParSim_t_ Gia_ParSim_t;
struct Gia_ParSim_t_
{
    // user-controlled parameters
    int            nWords;        // the number of machine words
    int            nIters;        // the number of timeframes
    int            RandSeed;      // seed to generate random numbers
    int            TimeLimit;     // time limit in seconds
    int            fCheckMiter;   // check if miter outputs are non-zero
    int            fVerbose;      // enables verbose output
    int            iOutFail;      // index of the failed output
};

typedef struct Gia_ManSim_t_ Gia_ManSim_t;
struct Gia_ManSim_t_
{
    Gia_Man_t *    pAig;
    Gia_ParSim_t * pPars; 
    int            nWords;
    Vec_Int_t *    vCis2Ids;
    Vec_Int_t *    vConsts;
    // simulation information
    unsigned *     pDataSim;     // simulation data
    unsigned *     pDataSimCis;  // simulation data for CIs
    unsigned *     pDataSimCos;  // simulation data for COs
};

typedef struct Jf_Par_t_ Jf_Par_t; 
struct Jf_Par_t_
{
    int            nLutSize;
    int            nCutNum;
    int            nProcNum;
    int            nRounds;
    int            nRoundsEla;
    int            nRelaxRatio;
    int            nCoarseLimit;
    int            nAreaTuner;
    int            nReqTimeFlex;
    int            nVerbLimit;
    int            nDelayLut1;
    int            nDelayLut2;
    int            nFastEdges;
    int            DelayTarget;
    int            fAreaOnly;
    int            fPinPerm;
    int            fPinQuick;
    int            fPinFilter;
    int            fOptEdge;
    int            fUseMux7;
    int            fPower;
    int            fCoarsen;
    int            fCutMin;
    int            fFuncDsd;
    int            fGenCnf;
    int            fGenLit;
    int            fCnfObjIds;
    int            fAddOrCla;
    int            fCnfMapping;
    int            fPureAig;
    int            fDoAverage;
    int            fCutHashing;
    int            fCutSimple;
    int            fCutGroup;
    int            fVerbose;
    int            fVeryVerbose;
    int            nLutSizeMax;
    int            nCutNumMax;
    int            nProcNumMax;
    int            nLutSizeMux;
    int            nMaxMatches;
    word           Delay;
    word           Area;
    word           Edge;
    word           Clause;
    word           Mux7;
    word           WordMapDelay;
    word           WordMapArea;
    word           WordMapDelayTarget;
    int            MapDelay;
    float          MapArea;
    float          MapAreaF;
    float          MapDelayTarget;
    float          Epsilon;
    float *        pTimesArr;
    float *        pTimesReq;
    char *         ZFile;
};

static inline unsigned     Gia_ObjCutSign( unsigned ObjId )       { return (1 << (ObjId & 31));                                 }
static inline int          Gia_WordHasOneBit( unsigned uWord )    { return (uWord & (uWord-1)) == 0;                            }
static inline int          Gia_WordHasOnePair( unsigned uWord )   { return Gia_WordHasOneBit(uWord & (uWord>>1) & 0x55555555);  }
static inline int          Gia_WordCountOnes( unsigned uWord )
{
    uWord = (uWord & 0x55555555) + ((uWord>>1) & 0x55555555);
    uWord = (uWord & 0x33333333) + ((uWord>>2) & 0x33333333);
    uWord = (uWord & 0x0F0F0F0F) + ((uWord>>4) & 0x0F0F0F0F);
    uWord = (uWord & 0x00FF00FF) + ((uWord>>8) & 0x00FF00FF);
    return  (uWord & 0x0000FFFF) + (uWord>>16);
}
static inline int Gia_WordFindFirstBit( unsigned uWord )
{
    int i;
    for ( i = 0; i < 32; i++ )
        if ( uWord & (1 << i) )
            return i;
    return -1;
}

static inline int Gia_ManTruthIsConst0( unsigned * pIn, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        if ( pIn[w] )
            return 0;
    return 1;
}
static inline int Gia_ManTruthIsConst1( unsigned * pIn, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        if ( pIn[w] != ~(unsigned)0 )
            return 0;
    return 1;
}
static inline void Gia_ManTruthCopy( unsigned * pOut, unsigned * pIn, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        pOut[w] = pIn[w];
}
static inline void Gia_ManTruthClear( unsigned * pOut, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        pOut[w] = 0;
}
static inline void Gia_ManTruthFill( unsigned * pOut, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        pOut[w] = ~(unsigned)0;
}
static inline void Gia_ManTruthNot( unsigned * pOut, unsigned * pIn, int nVars )
{
    int w;
    for ( w = Abc_TruthWordNum(nVars)-1; w >= 0; w-- )
        pOut[w] = ~pIn[w];
}

// GIA carries two parallel ways of naming a possibly inverted edge, and they must never be
// mixed in one value. The first is the LITERAL: a plain int holding an object identifier in
// its upper bits and the inversion in bit 0, built with Abc_Var2Lit and taken apart with
// Abc_Lit2Var and Abc_LitIsCompl. Because the constant-0 object always sits at identifier 0,
// literal 0 is constant 0 and literal 1 is constant 1, which is why the two constant tests
// below are exact comparisons and Gia_ManIsConstLit collapses them into iLit <= 1. Every
// append constructor further down this file returns Gia_ObjId(p, pObj) << 1, that is, an
// uncomplemented literal - a caller wanting the inverted edge complements the returned value
// itself. Literals are the form stored in side tables (Value, vCopies, pMuxes) because they
// survive reallocation of the object array, which addresses do not.
static inline int          Gia_ManConst0Lit()                  { return 0; }
static inline int          Gia_ManConst1Lit()                  { return 1; }
static inline int          Gia_ManIsConst0Lit( int iLit )      { return (iLit == 0); }
static inline int          Gia_ManIsConst1Lit( int iLit )      { return (iLit == 1); }
static inline int          Gia_ManIsConstLit( int iLit )       { return (iLit <= 1); }

// The second convention is the TAGGED POINTER: the inversion rides in bit 0 of a Gia_Obj_t *
// itself, so a single pointer-sized value names both the object and the polarity. This is
// available because Gia_Obj_t is built entirely from unsigned bit fields and one unsigned, so
// every object address inside the array is aligned to at least the alignment of unsigned and
// bit 0 of such an address is always clear; note that it is the alignment, not the twelve-byte
// size, that frees the bit. The four helpers below are pure bit twiddling on ABC_PTRUINT_T:
// Gia_Regular clears the tag, Gia_Not flips it, Gia_NotCond flips it conditionally, and
// Gia_IsComplement reads it. A tagged pointer must be passed through Gia_Regular before any
// field of the object is touched, since the tagged value is not a valid object address.
// Gia_Obj2Lit and Gia_Lit2Obj below are the explicit bridge between the two conventions.
static inline Gia_Obj_t *  Gia_Regular( Gia_Obj_t * p )        { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) & ~01);                           }
static inline Gia_Obj_t *  Gia_Not( Gia_Obj_t * p )            { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) ^  01);                           }
static inline Gia_Obj_t *  Gia_NotCond( Gia_Obj_t * p, int c ) { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) ^ (c));                           }
static inline int          Gia_IsComplement( Gia_Obj_t * p )   { return (int)((ABC_PTRUINT_T)(p) & 01);                                    }

// Sizes of the network. Most of these are derived on the spot from stored quantities rather
// than cached, which is why they are safe to call at any point during construction: only
// nRegs, nXors, nMuxes, nBufs and nConstrs are fields, and the constructors below maintain
// them. Three of the derivations depend on the ordering discipline the package expects.
// Gia_ManPiNum subtracts nRegs from the combinational-input count because the primary inputs
// occupy the front of vCis and the flop outputs its tail; Gia_ManPoNum does the same for vCos.
// Gia_ManAndNum takes everything that is neither a terminal nor the constant object - the
// trailing - 1 is the constant-0 object at identifier 0 - so it counts buffers, real XOR and
// real MUX objects too, since each of those occupies one object; Gia_ManAndNotBufNum removes
// the buffers again. Gia_ManCandNum is the combinational inputs plus that AND count, which is
// every object except the constant and the combinational outputs. Gia_ManHasChoices is only a
// null test on the pSibls side array, while Gia_ManChoiceNum walks the whole array and is
// therefore linear in the object count rather than a stored total.
static inline char *       Gia_ManName( Gia_Man_t * p )        { return p->pName;                                                          }
static inline int          Gia_ManCiNum( Gia_Man_t * p )       { return Vec_IntSize(p->vCis);                                              }
static inline int          Gia_ManCoNum( Gia_Man_t * p )       { return Vec_IntSize(p->vCos);                                              }
static inline int          Gia_ManPiNum( Gia_Man_t * p )       { return Vec_IntSize(p->vCis) - p->nRegs;                                   }
static inline int          Gia_ManPoNum( Gia_Man_t * p )       { return Vec_IntSize(p->vCos) - p->nRegs;                                   }
static inline int          Gia_ManRegNum( Gia_Man_t * p )      { return p->nRegs;                                                          }
static inline int          Gia_ManObjNum( Gia_Man_t * p )      { return p->nObjs;                                                          }
static inline int          Gia_ManAndNum( Gia_Man_t * p )      { return p->nObjs - Vec_IntSize(p->vCis) - Vec_IntSize(p->vCos) - 1;        }
static inline int          Gia_ManXorNum( Gia_Man_t * p )      { return p->nXors;                                                          }
static inline int          Gia_ManMuxNum( Gia_Man_t * p )      { return p->nMuxes;                                                         }
static inline int          Gia_ManBufNum( Gia_Man_t * p )      { return p->nBufs;                                                          }
static inline int          Gia_ManAndNotBufNum( Gia_Man_t * p ){ return Gia_ManAndNum(p) - Gia_ManBufNum(p);                               }
static inline int          Gia_ManCandNum( Gia_Man_t * p )     { return Gia_ManCiNum(p) + Gia_ManAndNum(p);                                }
static inline int          Gia_ManConstrNum( Gia_Man_t * p )   { return p->nConstrs;                                                       }
static inline void         Gia_ManFlipVerbose( Gia_Man_t * p ) { p->fVerbose ^= 1;                                                         } 
static inline int          Gia_ManHasChoices( Gia_Man_t * p )  { return p->pSibls != NULL;                                                 } 
static inline int          Gia_ManChoiceNum( Gia_Man_t * p )   { int c = 0; if (p->pSibls) { int i; for (i = 0; i < p->nObjs; i++) c += (int)(p->pSibls[i] > 0); } return c; } 

// Getting at objects. Gia_ManConst0 is the first slot of the object array, the one
// Gia_ManStart fills with GIA_NONE in both offsets (giaMan.c:L64); there is no second object
// for the constant 1, and Gia_ManConst1 returns the very same address with the tag bit set, so
// its result is a tagged pointer that must go through Gia_Regular before any field is read.
// Gia_ManObj converts an identifier back into an address and is the exact inverse of
// Gia_ObjId below, range-asserted the same way. Gia_ManCi and Gia_ManCo go through the vCis
// and vCos identifier lists rather than scanning, and the four role accessors sit on top of
// them: Gia_ManPi and Gia_ManPo index the front sections, while Gia_ManRo and Gia_ManRi skip
// past the primary counts into the flop sections. Flop output v and flop input v therefore
// belong to the same register purely by position in those two lists.
static inline Gia_Obj_t *  Gia_ManConst0( Gia_Man_t * p )      { return p->pObjs;                                                          }
static inline Gia_Obj_t *  Gia_ManConst1( Gia_Man_t * p )      { return Gia_Not(Gia_ManConst0(p));                                         }
static inline Gia_Obj_t *  Gia_ManObj( Gia_Man_t * p, int v )  { assert( v >= 0 && v < p->nObjs ); return p->pObjs + v;                    }
static inline Gia_Obj_t *  Gia_ManCi( Gia_Man_t * p, int v )   { return Gia_ManObj( p, Vec_IntEntry(p->vCis,v) );                          }
static inline Gia_Obj_t *  Gia_ManCo( Gia_Man_t * p, int v )   { return Gia_ManObj( p, Vec_IntEntry(p->vCos,v) );                          }
static inline Gia_Obj_t *  Gia_ManPi( Gia_Man_t * p, int v )   { assert( v < Gia_ManPiNum(p) );  return Gia_ManCi( p, v );                 }
static inline Gia_Obj_t *  Gia_ManPo( Gia_Man_t * p, int v )   { assert( v < Gia_ManPoNum(p) );  return Gia_ManCo( p, v );                 }
static inline Gia_Obj_t *  Gia_ManRo( Gia_Man_t * p, int v )   { assert( v < Gia_ManRegNum(p) ); return Gia_ManCi( p, Gia_ManPiNum(p)+v ); }
static inline Gia_Obj_t *  Gia_ManRi( Gia_Man_t * p, int v )   { assert( v < Gia_ManRegNum(p) ); return Gia_ManCo( p, Gia_ManPoNum(p)+v ); }

// Identity, value, phase and names.
// Gia_ObjId is the whole of GIA's naming scheme: an object's identifier is its distance from
// the base of the array, obtained by pointer subtraction and guarded by a range assertion. An
// identifier is therefore meaningful only against the manager it came from, and passing an
// object of one manager to another manager's accessor is caught by that assertion rather than
// silently producing a number. Gia_ObjCioId and Gia_ObjSetCioId are the terminal overload of
// the second offset field: both assert fTerm first, because on a terminal iDiff1 is not an
// offset at all but the object's position in vCis or vCos.
// Gia_ObjPhase reads the stored bit directly, so it expects a regular pointer, whereas
// Gia_ObjPhaseReal expects a tagged pointer and folds the tag into the answer - the two are
// not interchangeable. Gia_ObjPhaseDiff compares two objects given by identifier.
// The four name accessors are null-safe: each tests its vector and returns NULL when the
// manager carries no names, so callers need no test of their own. Gia_ObjNameObj shows that
// vNamesNode is indexed by object identifier; vNamesIn and vNamesOut are indexed by whatever
// numbering the caller passes in, which in practice is the combinational-input or -output
// index rather than an object identifier.
static inline int          Gia_ObjId( Gia_Man_t * p, Gia_Obj_t * pObj )        { assert( p->pObjs <= pObj && pObj < p->pObjs + p->nObjs ); return pObj - p->pObjs; }
static inline int          Gia_ObjCioId( Gia_Obj_t * pObj )                    { assert( pObj->fTerm ); return pObj->iDiff1;                 }
static inline void         Gia_ObjSetCioId( Gia_Obj_t * pObj, int v )          { assert( pObj->fTerm ); pObj->iDiff1 = v;                    }
static inline int          Gia_ObjValue( Gia_Obj_t * pObj )                    { return pObj->Value;                                         }
static inline void         Gia_ObjSetValue( Gia_Obj_t * pObj, int i )          { pObj->Value = i;                                            }
static inline int          Gia_ObjPhase( Gia_Obj_t * pObj )                    { return pObj->fPhase;                                        }
static inline int          Gia_ObjPhaseReal( Gia_Obj_t * pObj )                { return Gia_Regular(pObj)->fPhase ^ Gia_IsComplement(pObj);  }
static inline int          Gia_ObjPhaseDiff( Gia_Man_t * p, int i, int k )     { return Gia_ManObj(p, i)->fPhase ^ Gia_ManObj(p, k)->fPhase; }
static inline char *       Gia_ObjCiName( Gia_Man_t * p, int i )               { return p->vNamesIn   ? (char*)Vec_PtrEntry(p->vNamesIn, i)   : NULL; }
static inline char *       Gia_ObjCoName( Gia_Man_t * p, int i )               { return p->vNamesOut  ? (char*)Vec_PtrEntry(p->vNamesOut, i)  : NULL; }
static inline char *       Gia_ObjName( Gia_Man_t * p, int i )                 { return p->vNamesNode ? (char*)Vec_PtrEntry(p->vNamesNode, i) : NULL; }
static inline char *       Gia_ObjNameObj( Gia_Man_t * p, Gia_Obj_t * pObj )   { return p->vNamesNode ? (char*)Vec_PtrEntry(p->vNamesNode, Gia_ObjId(p, pObj)) : NULL; }

// TELLING THE KINDS APART. There is no type-tag field anywhere in Gia_Obj_t. The twelve bytes
// are two offsets, four flag bits and one full word, and every question about an object's kind
// is answered from three things only: the fTerm bit, the GIA_NONE sentinel in the offsets, and
// the relative ORDER of the two offsets. That is the price of the twelve-byte object, and it
// means these predicates rather than the struct declaration are the definition of "what kind".
//   fTerm set,   iDiff0 == GIA_NONE  -> combinational input
//   fTerm set,   iDiff0 != GIA_NONE  -> combinational output, its one fanin in iDiff0
//   fTerm clear, both  == GIA_NONE   -> the constant-0 object
//   fTerm clear, iDiff0 != GIA_NONE  -> an internal node, and then the ordering decides:
//        iDiff0 >  iDiff1            -> AND, or a real MUX when pMuxes marks the identifier
//        iDiff0 <  iDiff1            -> real XOR
//        iDiff0 == iDiff1            -> buffer
// DISPATCH ORDER MATTERS. Gia_ObjIsAnd is true for a buffer, for a real XOR and for a real MUX
// as well as for a plain AND, because all four are internal nodes with a fanin. An if/else
// chain that tests it first therefore swallows the other three, and the narrower tests have to
// come first. Gia_ManDupMarked is the in-tree example that gets the order right: buffer, then
// XOR, then MUX, then plain AND (giaDup.c:L1478-1488).
// Two asymmetries in this block are worth knowing. Gia_ObjIsBuf and Gia_ObjIsConst0 read the
// raw fields instead of building on Gia_ObjIsAnd, so they are safe on any object; but
// Gia_ObjIsConst0 answers by encoding rather than by position, making it true of any object
// that carries GIA_NONE in both offsets, whereas Gia_ManObjIsConst0 compares the address with
// the array base and so is true of exactly one object. And Gia_ObjIsMuxId, Gia_ObjIsMux and
// Gia_ObjIsAndReal take the manager because MUX-ness is not in the object at all: it lives in
// the pMuxes side array, so the very same twelve bytes read as a real AND in a manager without
// pMuxes and as a real MUX in one that marks that identifier.
// Gia_ObjIsCand is the union of the two kinds that carry a value of their own, internal nodes
// and combinational inputs, and Gia_ObjIsAndOrConst0 is just the negation of Gia_ObjIsTerm.
static inline int          Gia_ObjIsTerm( Gia_Obj_t * pObj )                   { return pObj->fTerm;                             } 
static inline int          Gia_ObjIsAndOrConst0( Gia_Obj_t * pObj )            { return!pObj->fTerm;                             } 
static inline int          Gia_ObjIsCi( Gia_Obj_t * pObj )                     { return pObj->fTerm && pObj->iDiff0 == GIA_NONE; } 
static inline int          Gia_ObjIsCo( Gia_Obj_t * pObj )                     { return pObj->fTerm && pObj->iDiff0 != GIA_NONE; } 
static inline int          Gia_ObjIsAnd( Gia_Obj_t * pObj )                    { return!pObj->fTerm && pObj->iDiff0 != GIA_NONE; } 
static inline int          Gia_ObjIsXor( Gia_Obj_t * pObj )                    { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 < pObj->iDiff1; } // the offset order IS the XOR tag
static inline int          Gia_ObjIsMuxId( Gia_Man_t * p, int iObj )           { return p->pMuxes && p->pMuxes[iObj] > 0;        } 
static inline int          Gia_ObjIsMux( Gia_Man_t * p, Gia_Obj_t * pObj )     { return Gia_ObjIsMuxId( p, Gia_ObjId(p, pObj) ); } 
static inline int          Gia_ObjIsAndReal( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 > pObj->iDiff1 && !Gia_ObjIsMux(p, pObj); } // same bits as a real MUX; pMuxes separates them
static inline int          Gia_ObjIsBuf( Gia_Obj_t * pObj )                    { return pObj->iDiff0 == pObj->iDiff1 && pObj->iDiff0 != GIA_NONE && !pObj->fTerm;    } // equal non-sentinel offsets, not a terminal
static inline int          Gia_ObjIsAndNotBuf( Gia_Obj_t * pObj )              { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 != pObj->iDiff1; } // exists because Gia_ObjIsAnd is true for buffers
static inline int          Gia_ObjIsCand( Gia_Obj_t * pObj )                   { return Gia_ObjIsAnd(pObj) || Gia_ObjIsCi(pObj); } 
static inline int          Gia_ObjIsConst0( Gia_Obj_t * pObj )                 { return pObj->iDiff0 == GIA_NONE && pObj->iDiff1 == GIA_NONE;     } 
static inline int          Gia_ManObjIsConst0( Gia_Man_t * p, Gia_Obj_t * pObj){ return pObj == p->pObjs;                        } 

// The bridge between the two conventions introduced above. Gia_Obj2Lit takes a TAGGED pointer,
// strips the tag to get a legal address, turns that into an identifier and folds the tag back
// in as bit 0. Gia_Lit2Obj is the inverse and hands back a tagged pointer, so its result must
// be regularized before any field is read. Gia_ManCiLit is the shorthand a builder wants: the
// literal of a combinational input given its position in vCis.
static inline int          Gia_Obj2Lit( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Abc_Var2Lit(Gia_ObjId(p, Gia_Regular(pObj)), Gia_IsComplement(pObj)); }
static inline Gia_Obj_t *  Gia_Lit2Obj( Gia_Man_t * p, int iLit )              { return Gia_NotCond(Gia_ManObj(p, Abc_Lit2Var(iLit)), Abc_LitIsCompl(iLit));  }
static inline int          Gia_ManCiLit( Gia_Man_t * p, int CiId )             { return Gia_Obj2Lit( p, Gia_ManCi(p, CiId) );                }

// Two numbering spaces meet here and are easy to confuse: an OBJECT IDENTIFIER indexes pObjs,
// while a CIO INDEX indexes vCis or vCos. Gia_ManIdToCioId goes from the first to the second by
// reading the terminal overload of iDiff1, and so inherits the fTerm assertion of Gia_ObjCioId;
// Gia_ManCiIdToId and Gia_ManCoIdToId go the other way through the identifier lists. Nothing in
// the types distinguishes the two spaces - both are int - so which one a variable holds is
// carried by its name alone.
static inline int          Gia_ManIdToCioId( Gia_Man_t * p, int Id )           { return Gia_ObjCioId( Gia_ManObj(p, Id) );                   }
static inline int          Gia_ManCiIdToId( Gia_Man_t * p, int CiId )          { return Gia_ObjId( p, Gia_ManCi(p, CiId) );                  }
static inline int          Gia_ManCoIdToId( Gia_Man_t * p, int CoId )          { return Gia_ObjId( p, Gia_ManCo(p, CoId) );                  }

// The ROLE of a terminal - primary input or flop output, primary output or flop input - is not
// a stored bit either. It is a range test on the CIO index, which works only because the front
// of vCis holds every primary input and its tail every flop output, and likewise for vCos. That
// is the whole reason the construction order matters: append all primary inputs before any flop
// output, all primary outputs before any flop input, then declare the register count once. Note
// that each of the four leads with Gia_ObjIsCi or Gia_ObjIsCo, so the short-circuit is what
// keeps the following Gia_ObjCioId - which asserts fTerm - from firing on an internal node.
static inline int          Gia_ObjIsPi( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCi(pObj) && Gia_ObjCioId(pObj) < Gia_ManPiNum(p);   } 
static inline int          Gia_ObjIsPo( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCo(pObj) && Gia_ObjCioId(pObj) < Gia_ManPoNum(p);   } 
static inline int          Gia_ObjIsRo( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCi(pObj) && Gia_ObjCioId(pObj) >= Gia_ManPiNum(p);  } 
static inline int          Gia_ObjIsRi( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCo(pObj) && Gia_ObjCioId(pObj) >= Gia_ManPoNum(p);  } 

// Pairing the two halves of a register. A flop is one combinational input and one combinational
// output, matched by position within their tail sections, and the shift below is that pairing in
// closed form: since Gia_ManPiNum is Gia_ManCiNum - nRegs and Gia_ManPoNum is Gia_ManCoNum -
// nRegs, adding Gia_ManCoNum - Gia_ManCiNum to a CI index i lands on CO index Gia_ManPoNum +
// (i - Gia_ManPiNum), and the reverse shift undoes it. Both assert the role first, so they
// cannot be used to probe whether an object is a flop half - test with Gia_ObjIsRo or
// Gia_ObjIsRi instead. The two identifier forms merely wrap these in Gia_ManObj and Gia_ObjId.
static inline Gia_Obj_t *  Gia_ObjRoToRi( Gia_Man_t * p, Gia_Obj_t * pObj )    { assert( Gia_ObjIsRo(p, pObj) ); return Gia_ManCo(p, Gia_ManCoNum(p) - Gia_ManCiNum(p) + Gia_ObjCioId(pObj)); } 
static inline Gia_Obj_t *  Gia_ObjRiToRo( Gia_Man_t * p, Gia_Obj_t * pObj )    { assert( Gia_ObjIsRi(p, pObj) ); return Gia_ManCi(p, Gia_ManCiNum(p) - Gia_ManCoNum(p) + Gia_ObjCioId(pObj)); } 
static inline int          Gia_ObjRoToRiId( Gia_Man_t * p, int ObjId )         { return Gia_ObjId( p, Gia_ObjRoToRi( p, Gia_ManObj(p, ObjId) ) );    } 
static inline int          Gia_ObjRiToRoId( Gia_Man_t * p, int ObjId )         { return Gia_ObjId( p, Gia_ObjRiToRo( p, Gia_ManObj(p, ObjId) ) );    }

// RESOLVING FANINS. This block is where the offset encoding is actually cashed in, and the
// arithmetic is the same in every member of it: a fanin is stored as a BACKWARD DISTANCE in
// units of Gia_Obj_t, so the fanin of pObj is at pObj - pObj->iDiff0 and its identifier is
// ObjId - pObj->iDiff0. Nothing here dereferences a stored address, because none is stored.
// Three consequences follow directly from the subtraction and are worth stating once:
//  - Both fanins of an object necessarily lie at LOWER identifiers than the object itself, so
//    topological order is a property of the encoding rather than a convention a pass upholds.
//    The encoding cannot express a forward reference at all.
//  - The offsets are position-independent, so they stay correct when Gia_ManAppendObj below
//    reallocates the object array and every object moves in memory. Any Gia_Obj_t * a caller
//    was holding does not survive that move; an identifier does.
//  - A distance is only meaningful against the array it was measured in, which is why objects
//    are never migrated between managers - duplication rebuilds them instead.
// FORM CONVENTIONS across the block. Members named ...Fanin0/...Fanin1 return addresses;
// ...FaninId0/...FaninId1 do the same arithmetic on ints and take the object's OWN identifier
// as a parameter, while the ...Id0p/...Id1p forms recover that identifier themselves through
// Gia_ObjId and so cost one extra subtraction; ...FaninLit* compose identifier and complement
// bit into a literal; ...Child* compose address and complement bit into a tagged pointer. The
// generic ...Fanin( pObj, n ) selectors pick fanin 0 or 1 from an int index.
// THE THIRD FANIN IS DIFFERENT. Gia_ObjFanin2, Gia_ObjFaninC2, Gia_ObjFaninId2, Gia_ObjFaninLit2
// and their p-forms read the pMuxes side array, not the object, and they test only whether that
// array EXISTS - not whether this object is a MUX. In a manager that carries pMuxes, asking for
// fanin 2 of a plain AND reads that identifier's slot, which is zero, and reports object 0 with
// no complement rather than reporting "no such fanin". Their missing answers also differ in
// type: Gia_ObjFanin2 yields NULL when pMuxes is absent, whereas the identifier and literal
// forms yield -1. Callers that care must ask Gia_ObjIsMux first.
// Gia_ObjFaninNum reports 3 for a MUX, 2 for an internal node, 1 for a combinational output and
// 0 otherwise, and it needs the manager only for the MUX question; because it has no buffer
// case, a buffer is reported as having two fanins even though its two offsets are equal.
// Gia_ObjFlipFaninC0 is the one sanctioned in-place mutation of an edge in this file, and it
// asserts a combinational output, so no structurally hashed node can be altered through it.
// Gia_ObjWhatFanin answers "which input of pObj is pFanin" and ends in assert(0) and -1: it is
// defined only for an object that really is a fanin, and is not a membership test.
static inline int          Gia_ObjDiff0( Gia_Obj_t * pObj )                    { return pObj->iDiff0;         }
static inline int          Gia_ObjDiff1( Gia_Obj_t * pObj )                    { return pObj->iDiff1;         }
static inline int          Gia_ObjFaninC0( Gia_Obj_t * pObj )                  { return pObj->fCompl0;        }
static inline int          Gia_ObjFaninC1( Gia_Obj_t * pObj )                  { return pObj->fCompl1;        }
static inline int          Gia_ObjFaninC2( Gia_Man_t * p, Gia_Obj_t * pObj )   { return p->pMuxes && Abc_LitIsCompl(p->pMuxes[Gia_ObjId(p, pObj)]);  }
static inline int          Gia_ObjFaninC( Gia_Obj_t * pObj, int n )            { return n ? Gia_ObjFaninC1(pObj) : Gia_ObjFaninC0(pObj);             }
static inline Gia_Obj_t *  Gia_ObjFanin0( Gia_Obj_t * pObj )                   { return pObj - pObj->iDiff0;  } // pointer arithmetic, not a stored address
static inline Gia_Obj_t *  Gia_ObjFanin1( Gia_Obj_t * pObj )                   { return pObj - pObj->iDiff1;  } // subtraction is why fanins precede the object
static inline Gia_Obj_t *  Gia_ObjFanin2( Gia_Man_t * p, Gia_Obj_t * pObj )    { return p->pMuxes ? Gia_ManObj(p, Abc_Lit2Var(p->pMuxes[Gia_ObjId(p, pObj)])) : NULL;  }
static inline Gia_Obj_t *  Gia_ObjFanin( Gia_Obj_t * pObj, int n )             { return n ? Gia_ObjFanin1(pObj) : Gia_ObjFanin0(pObj);               }
static inline Gia_Obj_t *  Gia_ObjChild0( Gia_Obj_t * pObj )                   { return Gia_NotCond( Gia_ObjFanin0(pObj), Gia_ObjFaninC0(pObj) ); }
static inline Gia_Obj_t *  Gia_ObjChild1( Gia_Obj_t * pObj )                   { return Gia_NotCond( Gia_ObjFanin1(pObj), Gia_ObjFaninC1(pObj) ); }
static inline Gia_Obj_t *  Gia_ObjChild2( Gia_Man_t * p, Gia_Obj_t * pObj )    { return Gia_NotCond( Gia_ObjFanin2(p, pObj), Gia_ObjFaninC2(p, pObj) ); }
static inline int          Gia_ObjFaninId0( Gia_Obj_t * pObj, int ObjId )      { return ObjId - pObj->iDiff0;    } // the same delta, applied to identifiers
static inline int          Gia_ObjFaninId1( Gia_Obj_t * pObj, int ObjId )      { return ObjId - pObj->iDiff1;    }
static inline int          Gia_ObjFaninId2( Gia_Man_t * p, int ObjId )         { return (p->pMuxes && p->pMuxes[ObjId]) ? Abc_Lit2Var(p->pMuxes[ObjId]) : -1; }
static inline int          Gia_ObjFaninId( Gia_Obj_t * pObj, int ObjId, int n ){ return n ? Gia_ObjFaninId1(pObj, ObjId) : Gia_ObjFaninId0(pObj, ObjId);      }
static inline int          Gia_ObjFaninId0p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjFaninId0( pObj, Gia_ObjId(p, pObj) );              }
static inline int          Gia_ObjFaninId1p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjFaninId1( pObj, Gia_ObjId(p, pObj) );              }
static inline int          Gia_ObjFaninId2p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return (p->pMuxes && p->pMuxes[Gia_ObjId(p, pObj)]) ? Abc_Lit2Var(p->pMuxes[Gia_ObjId(p, pObj)]) : -1; }
static inline int          Gia_ObjFaninIdp( Gia_Man_t * p, Gia_Obj_t * pObj, int n){ return n ? Gia_ObjFaninId1p(p, pObj) : Gia_ObjFaninId0p(p, pObj);     }
static inline int          Gia_ObjFaninLit0( Gia_Obj_t * pObj, int ObjId )     { return Abc_Var2Lit( Gia_ObjFaninId0(pObj, ObjId), Gia_ObjFaninC0(pObj) ); }
static inline int          Gia_ObjFaninLit1( Gia_Obj_t * pObj, int ObjId )     { return Abc_Var2Lit( Gia_ObjFaninId1(pObj, ObjId), Gia_ObjFaninC1(pObj) ); }
static inline int          Gia_ObjFaninLit2( Gia_Man_t * p, int ObjId )        { return (p->pMuxes && p->pMuxes[ObjId]) ? p->pMuxes[ObjId] : -1;           }
static inline int          Gia_ObjFaninLit( Gia_Obj_t * pObj, int ObjId, int n ){ return n ? Gia_ObjFaninLit1(pObj, ObjId) : Gia_ObjFaninLit0(pObj, ObjId);}
static inline int          Gia_ObjFaninLit0p( Gia_Man_t * p, Gia_Obj_t * pObj) { return Abc_Var2Lit( Gia_ObjFaninId0p(p, pObj), Gia_ObjFaninC0(pObj) );    }
static inline int          Gia_ObjFaninLit1p( Gia_Man_t * p, Gia_Obj_t * pObj) { return Abc_Var2Lit( Gia_ObjFaninId1p(p, pObj), Gia_ObjFaninC1(pObj) );    }
static inline int          Gia_ObjFaninLit2p( Gia_Man_t * p, Gia_Obj_t * pObj) { return (p->pMuxes && p->pMuxes[Gia_ObjId(p, pObj)]) ? p->pMuxes[Gia_ObjId(p, pObj)] : -1;    }
static inline int          Gia_ObjFaninLitp( Gia_Man_t * p, Gia_Obj_t * pObj, int n ){ return n ? Gia_ObjFaninLit1p(p, pObj) : Gia_ObjFaninLit0p(p, pObj);}
static inline void         Gia_ObjFlipFaninC0( Gia_Obj_t * pObj )              { assert( Gia_ObjIsCo(pObj) ); pObj->fCompl0 ^= 1;          }
static inline int          Gia_ObjFaninNum( Gia_Man_t * p, Gia_Obj_t * pObj )  { if ( Gia_ObjIsMux(p, pObj) ) return 3; if ( Gia_ObjIsAnd(pObj) ) return 2; if ( Gia_ObjIsCo(pObj) ) return 1; return 0; }
static inline int          Gia_ObjWhatFanin( Gia_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pFanin )  { if ( Gia_ObjFanin0(pObj) == pFanin ) return 0; if ( Gia_ObjFanin1(pObj) == pFanin ) return 1; if ( Gia_ObjFanin2(p, pObj) == pFanin ) return 2; assert(0); return -1; }

// What drives an output. A combinational output has exactly one fanin, always in iDiff0, so
// "the driver" is unambiguous and these four all reach it through fanin 0. Gia_ManPoIsConst
// asks only whether the driver is the constant object and so is true for a primary output tied
// to either constant, while Gia_ManPoIsConst0 and Gia_ManPoIsConst1 compare the whole driver
// LITERAL and therefore separate the two polarities. All three index the primary-output section
// of vCos, not vCos itself, so a flop input cannot be reached through them.
static inline int          Gia_ManCoDriverId( Gia_Man_t * p, int iCoIndex )    { return Gia_ObjFaninId0p(p, Gia_ManCo(p, iCoIndex));                        }
static inline int          Gia_ManPoIsConst( Gia_Man_t * p, int iPoIndex )     { return Gia_ObjFaninId0p(p, Gia_ManPo(p, iPoIndex)) == 0;                   }
static inline int          Gia_ManPoIsConst0( Gia_Man_t * p, int iPoIndex )    { return Gia_ManIsConst0Lit( Gia_ObjFaninLit0p(p, Gia_ManPo(p, iPoIndex)) ); }
static inline int          Gia_ManPoIsConst1( Gia_Man_t * p, int iPoIndex )    { return Gia_ManIsConst1Lit( Gia_ObjFaninLit0p(p, Gia_ManPo(p, iPoIndex)) ); }

// COPY MAPS. Rebuilding a network means remembering, for each object of the old manager, what
// it became in the new one. GIA offers three separate places to keep that mapping, and mixing
// them within one pass is what goes wrong most often. This first group uses the object's own
// Value field, which holds a LITERAL in the destination manager - identifier plus polarity, not
// an address - which is why Gia_ObjCopy has to run it through Abc_Lit2Var and why the manager it
// takes is the DESTINATION while pObj belongs to the source. Gia_ObjFanin0Copy and its siblings
// are the rebuild idiom in one expression: resolve the fanin in the source by offset, read the
// copy literal parked in its Value, and fold in the complement bit stored on the edge. Because
// the whole mapping rides in the objects, this form needs no side vector and no cleanup.
// The sentinel of this form is ~0, written by Gia_ManFillValue (giaUtil.c:L369-374) and tested
// as if ( ~pObj->Value ) (giaDup.c:L1751-1752). Note that Gia_ManCleanValue (giaUtil.c:L351-356)
// writes 0 instead, which is the perfectly valid literal for constant 0, so it does NOT create
// that sentinel and the two helpers are not interchangeable before a rebuild.
// Gia_ObjFanin2Copy inherits the third-fanin caveat from the block above: it dereferences the
// result of Gia_ObjFanin2, which is NULL in a manager that carries no pMuxes.
static inline Gia_Obj_t *  Gia_ObjCopy( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ManObj( p, Abc_Lit2Var(pObj->Value) );                              }
static inline int          Gia_ObjLitCopy( Gia_Man_t * p, int iLit )           { return Abc_LitNotCond( Gia_ManObj(p, Abc_Lit2Var(iLit))->Value, Abc_LitIsCompl(iLit));     }

static inline int          Gia_ObjFanin0Copy( Gia_Obj_t * pObj )               { return Abc_LitNotCond( Gia_ObjFanin0(pObj)->Value, Gia_ObjFaninC0(pObj) );     }
static inline int          Gia_ObjFanin1Copy( Gia_Obj_t * pObj )               { return Abc_LitNotCond( Gia_ObjFanin1(pObj)->Value, Gia_ObjFaninC1(pObj) );     }
static inline int          Gia_ObjFanin2Copy( Gia_Man_t * p, Gia_Obj_t * pObj ){ return Abc_LitNotCond(Gia_ObjFanin2(p, pObj)->Value, Gia_ObjFaninC2(p, pObj)); }

// The second and third places are side vectors, embedded by value so they are addressed as
// &p->vCopies and &p->vCopies2. The pair below is FRAME-INDEXED: the vector is treated as f
// planes of Gia_ManObjNum(p) entries each, so one object has one copy per time frame, which is
// what unrolling a sequential network needs. The four that follow are FLAT over the same
// vCopies vector - index is the bare object identifier - and Gia_ManCleanCopyArray sizes it for
// exactly one plane. The two patterns therefore share storage and cannot both be live in one
// pass: a frame-indexed writer needs the vector sized for all its planes, which the flat
// cleaner does not do. Gia_ObjCopy2Array and its two companions are a wholly independent flat
// map over vCopies2, for passes that need to keep two mappings at once.
// The missing-entry marker of both flat maps is -1, written by their clean helpers, which is a
// different convention from the ~0 used by the Value form above and from the GIA_NONE and
// GIA_VOID sentinels used inside objects. Nothing checks that a reader uses the right one.
static inline int          Gia_ObjCopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj )               { return Vec_IntEntry(&p->vCopies, Gia_ManObjNum(p) * f + Gia_ObjId(p,pObj));      }
static inline void         Gia_ObjSetCopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj, int iLit )  { Vec_IntWriteEntry(&p->vCopies, Gia_ManObjNum(p) * f + Gia_ObjId(p,pObj), iLit);  }
static inline int          Gia_ObjCopyArray( Gia_Man_t * p, int iObj )                          { return Vec_IntEntry(&p->vCopies, iObj);                                          }
static inline void         Gia_ObjSetCopyArray( Gia_Man_t * p, int iObj, int iLit )             { Vec_IntWriteEntry(&p->vCopies, iObj, iLit);                                      }
static inline void         Gia_ManCleanCopyArray( Gia_Man_t * p )                               { Vec_IntFill( &p->vCopies, Gia_ManObjNum(p), -1 );                                }

static inline int          Gia_ObjCopy2Array( Gia_Man_t * p, int iObj )                          { return Vec_IntEntry(&p->vCopies2, iObj);                                          }
static inline void         Gia_ObjSetCopy2Array( Gia_Man_t * p, int iObj, int iLit )             { Vec_IntWriteEntry(&p->vCopies2, iObj, iLit);                                      }
static inline void         Gia_ManCleanCopy2Array( Gia_Man_t * p )                               { Vec_IntFill( &p->vCopies2, Gia_ManObjNum(p), -1 );                                }

static inline int          Gia_ObjFanin0CopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj )         { return Abc_LitNotCond(Gia_ObjCopyF(p, f, Gia_ObjFanin0(pObj)), Gia_ObjFaninC0(pObj));   }
static inline int          Gia_ObjFanin1CopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj )         { return Abc_LitNotCond(Gia_ObjCopyF(p, f, Gia_ObjFanin1(pObj)), Gia_ObjFaninC1(pObj));   }
static inline int          Gia_ObjFanin0CopyArray( Gia_Man_t * p, Gia_Obj_t * pObj )            { return Abc_LitNotCond(Gia_ObjCopyArray(p, Gia_ObjFaninId0p(p,pObj)), Gia_ObjFaninC0(pObj));  }
static inline int          Gia_ObjFanin1CopyArray( Gia_Man_t * p, Gia_Obj_t * pObj )            { return Abc_LitNotCond(Gia_ObjCopyArray(p, Gia_ObjFaninId1p(p,pObj)), Gia_ObjFaninC1(pObj));  }

static inline Gia_Obj_t *  Gia_ObjFromLit( Gia_Man_t * p, int iLit )           { return Gia_NotCond( Gia_ManObj(p, Abc_Lit2Var(iLit)), Abc_LitIsCompl(iLit) );  }
static inline int          Gia_ObjToLit( Gia_Man_t * p, Gia_Obj_t * pObj )     { return Abc_Var2Lit( Gia_ObjId(p, Gia_Regular(pObj)), Gia_IsComplement(pObj) ); }
static inline int          Gia_ObjPhaseRealLit( Gia_Man_t * p, int iLit )      { return Gia_ObjPhaseReal( Gia_ObjFromLit(p, iLit) );                            }

static inline int          Gia_ObjLevelId( Gia_Man_t * p, int Id )             { return Vec_IntGetEntry(p->vLevels, Id);                    }
static inline int          Gia_ObjLevel( Gia_Man_t * p, Gia_Obj_t * pObj )     { return Gia_ObjLevelId( p, Gia_ObjId(p,pObj) );             }
static inline void         Gia_ObjUpdateLevelId( Gia_Man_t * p, int Id, int l )   { Vec_IntSetEntry(p->vLevels, Id, Abc_MaxInt(Vec_IntEntry(p->vLevels, Id), l));                        }
static inline void         Gia_ObjSetLevelId( Gia_Man_t * p, int Id, int l )   { Vec_IntSetEntry(p->vLevels, Id, l);                        }
static inline void         Gia_ObjSetLevel( Gia_Man_t * p, Gia_Obj_t * pObj, int l )  { Gia_ObjSetLevelId( p, Gia_ObjId(p,pObj), l );       }
static inline void         Gia_ObjSetCoLevel( Gia_Man_t * p, Gia_Obj_t * pObj )  { assert( Gia_ObjIsCo(pObj)  ); Gia_ObjSetLevel( p, pObj, Gia_ObjLevel(p,Gia_ObjFanin0(pObj)) );                                                }
static inline void         Gia_ObjSetBufLevel( Gia_Man_t * p, Gia_Obj_t * pObj ) { assert( Gia_ObjIsAnd(pObj) ); Gia_ObjSetLevel( p, pObj, Gia_ObjLevel(p,Gia_ObjFanin0(pObj)) );                                                }
static inline void         Gia_ObjSetAndLevel( Gia_Man_t * p, Gia_Obj_t * pObj ) { assert( Gia_ObjIsAnd(pObj) ); Gia_ObjSetLevel( p, pObj, 1+Abc_MaxInt(Gia_ObjLevel(p,Gia_ObjFanin0(pObj)),Gia_ObjLevel(p,Gia_ObjFanin1(pObj))) ); }
static inline void         Gia_ObjSetXorLevel( Gia_Man_t * p, Gia_Obj_t * pObj ) { assert( Gia_ObjIsXor(pObj) ); Gia_ObjSetLevel( p, pObj, 2+Abc_MaxInt(Gia_ObjLevel(p,Gia_ObjFanin0(pObj)),Gia_ObjLevel(p,Gia_ObjFanin1(pObj))) ); }
static inline void         Gia_ObjSetMuxLevel( Gia_Man_t * p, Gia_Obj_t * pObj ) { assert( Gia_ObjIsMux(p,pObj) ); Gia_ObjSetLevel( p, pObj, 2+Abc_MaxInt( Abc_MaxInt(Gia_ObjLevel(p,Gia_ObjFanin0(pObj)),Gia_ObjLevel(p,Gia_ObjFanin1(pObj))), Gia_ObjLevel(p,Gia_ObjFanin2(p,pObj))) ); }
static inline void         Gia_ObjSetGateLevel( Gia_Man_t * p, Gia_Obj_t * pObj ){ if ( !p->fGiaSimple && Gia_ObjIsBuf(pObj) ) Gia_ObjSetBufLevel(p, pObj); else if ( Gia_ObjIsMux(p,pObj) ) Gia_ObjSetMuxLevel(p, pObj); else if ( Gia_ObjIsXor(pObj) ) Gia_ObjSetXorLevel(p, pObj); else if ( Gia_ObjIsAnd(pObj) ) Gia_ObjSetAndLevel(p, pObj); }

static inline int          Gia_ObjHasNumId( Gia_Man_t * p, int Id )                { return Vec_IntEntry(p->vTtNums, Id) > -ABC_INFINITY;     }
static inline int          Gia_ObjNumId( Gia_Man_t * p, int Id )                   { return Vec_IntEntry(p->vTtNums, Id);                     }
static inline int          Gia_ObjNum( Gia_Man_t * p, Gia_Obj_t * pObj )           { return Vec_IntEntry(p->vTtNums, Gia_ObjId(p,pObj));      }
static inline void         Gia_ObjSetNumId( Gia_Man_t * p, int Id, int n )         { Vec_IntWriteEntry(p->vTtNums, Id, n);                    }
static inline void         Gia_ObjSetNum( Gia_Man_t * p, Gia_Obj_t * pObj, int n ) { Vec_IntWriteEntry(p->vTtNums, Gia_ObjId(p,pObj), n);     }
static inline void         Gia_ObjResetNumId( Gia_Man_t * p, int Id )              { Vec_IntWriteEntry(p->vTtNums, Id, -ABC_INFINITY);        }

static inline int          Gia_ObjRefNumId( Gia_Man_t * p, int Id )                { return p->pRefs[Id];                              }
static inline int          Gia_ObjRefIncId( Gia_Man_t * p, int Id )                { return p->pRefs[Id]++;                            }
static inline int          Gia_ObjRefDecId( Gia_Man_t * p, int Id )                { return --p->pRefs[Id];                            }
static inline int          Gia_ObjRefNum( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ObjRefNumId( p, Gia_ObjId(p, pObj) );  }
static inline int          Gia_ObjRefInc( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ObjRefIncId( p, Gia_ObjId(p, pObj) );  }
static inline int          Gia_ObjRefDec( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ObjRefDecId( p, Gia_ObjId(p, pObj) );  }
static inline void         Gia_ObjRefFanin0Inc(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefInc(p, Gia_ObjFanin0(pObj));            }
static inline void         Gia_ObjRefFanin1Inc(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefInc(p, Gia_ObjFanin1(pObj));            }
static inline void         Gia_ObjRefFanin2Inc(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefInc(p, Gia_ObjFanin2(p, pObj));         }
static inline void         Gia_ObjRefFanin0Dec(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefDec(p, Gia_ObjFanin0(pObj));            }
static inline void         Gia_ObjRefFanin1Dec(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefDec(p, Gia_ObjFanin1(pObj));            }
static inline void         Gia_ObjRefFanin2Dec(Gia_Man_t * p, Gia_Obj_t * pObj)    { Gia_ObjRefDec(p, Gia_ObjFanin2(p, pObj));         }

static inline int          Gia_ObjLutRefNumId( Gia_Man_t * p, int Id )             { assert(p->pLutRefs); return p->pLutRefs[Id];                    }
static inline int          Gia_ObjLutRefIncId( Gia_Man_t * p, int Id )             { assert(p->pLutRefs); return p->pLutRefs[Id]++;                  }
static inline int          Gia_ObjLutRefDecId( Gia_Man_t * p, int Id )             { assert(p->pLutRefs); return --p->pLutRefs[Id];                  }
static inline int          Gia_ObjLutRefNum( Gia_Man_t * p, Gia_Obj_t * pObj )     { assert(p->pLutRefs); return p->pLutRefs[Gia_ObjId(p, pObj)];    }
static inline int          Gia_ObjLutRefInc( Gia_Man_t * p, Gia_Obj_t * pObj )     { assert(p->pLutRefs); return p->pLutRefs[Gia_ObjId(p, pObj)]++;  }
static inline int          Gia_ObjLutRefDec( Gia_Man_t * p, Gia_Obj_t * pObj )     { assert(p->pLutRefs); return --p->pLutRefs[Gia_ObjId(p, pObj)];  }

// TRAVERSAL IDENTIFIERS: a visited mark that costs nothing to clear. A depth-first pass needs a
// "seen" bit per object, and clearing such a bit before every pass would be linear in the object
// count. Instead the manager keeps a generation counter, nTravIds, and a side array pTravIds of
// one int per object; an object counts as marked exactly when its entry EQUALS the current
// generation. Starting a new pass is then a single increment - Gia_ManIncrementTravId
// (giaUtil.c:L190-205), which also grows the array by doubling and zeroes the new half - and no
// per-object work at all, so the clear is O(1) rather than O(n). The same device reappears as
// iTimeStamp and vTimeStamps in the incremental-simulation block of the manager.
// Gia_ObjSetTravIdPrevious writes nTravIds - 1 rather than nTravIds, which is what gives two
// live generations at once: a pass can distinguish "reached in this round" from "reached in the
// previous round" without a second array. The two Update forms are test-and-set: each returns 1
// when the object was already marked and leaves it alone, otherwise marks it and returns 0, so
// the common recursion guard is a single call rather than a test followed by a set.
// Every member of this block asserts the identifier against nTravIdsAlloc rather than against
// the object count, because the array is grown by that same helper and not by the appenders; an
// object appended after the last increment is outside the allocation until the next one. The
// ...Id forms exist so that a caller holding only an identifier need not materialize an address.
static inline void         Gia_ObjSetTravIdCurrent( Gia_Man_t * p, Gia_Obj_t * pObj )         { assert( Gia_ObjId(p, pObj) < p->nTravIdsAlloc ); p->pTravIds[Gia_ObjId(p, pObj)] = p->nTravIds;                    }
static inline void         Gia_ObjSetTravIdPrevious( Gia_Man_t * p, Gia_Obj_t * pObj )        { assert( Gia_ObjId(p, pObj) < p->nTravIdsAlloc ); p->pTravIds[Gia_ObjId(p, pObj)] = p->nTravIds - 1;                }
static inline int          Gia_ObjIsTravIdCurrent( Gia_Man_t * p, Gia_Obj_t * pObj )          { assert( Gia_ObjId(p, pObj) < p->nTravIdsAlloc ); return (p->pTravIds[Gia_ObjId(p, pObj)] == p->nTravIds);          }
static inline int          Gia_ObjIsTravIdPrevious( Gia_Man_t * p, Gia_Obj_t * pObj )         { assert( Gia_ObjId(p, pObj) < p->nTravIdsAlloc ); return (p->pTravIds[Gia_ObjId(p, pObj)] == p->nTravIds - 1);      }
static inline int          Gia_ObjUpdateTravIdCurrent( Gia_Man_t * p, Gia_Obj_t * pObj )      { if ( Gia_ObjIsTravIdCurrent(p, pObj) )  return 1; Gia_ObjSetTravIdCurrent(p, pObj);  return 0; }
static inline int          Gia_ObjUpdateTravIdPrevious( Gia_Man_t * p, Gia_Obj_t * pObj )     { if ( Gia_ObjIsTravIdPrevious(p, pObj) ) return 1; Gia_ObjSetTravIdPrevious(p, pObj); return 0; }
static inline void         Gia_ObjSetTravIdCurrentId( Gia_Man_t * p, int Id )                 { assert( Id < p->nTravIdsAlloc ); p->pTravIds[Id] = p->nTravIds;                     }
static inline void         Gia_ObjSetTravIdPreviousId( Gia_Man_t * p, int Id )                { assert( Id < p->nTravIdsAlloc ); p->pTravIds[Id] = p->nTravIds - 1;                 }
static inline int          Gia_ObjIsTravIdCurrentId( Gia_Man_t * p, int Id )                  { assert( Id < p->nTravIdsAlloc ); return (p->pTravIds[Id] == p->nTravIds);           }
static inline int          Gia_ObjIsTravIdPreviousId( Gia_Man_t * p, int Id )                 { assert( Id < p->nTravIdsAlloc ); return (p->pTravIds[Id] == p->nTravIds - 1);       }
static inline int          Gia_ObjUpdateTravIdCurrentId( Gia_Man_t * p, int Id )              { if ( Gia_ObjIsTravIdCurrentId(p, Id) )  return 1; Gia_ObjSetTravIdCurrentId(p, Id);  return 0; }
static inline int          Gia_ObjUpdateTravIdPreviousId( Gia_Man_t * p, int Id )             { if ( Gia_ObjIsTravIdPreviousId(p, Id) ) return 1; Gia_ObjSetTravIdPreviousId(p, Id); return 0; }

static inline void         Gia_ManTimeClean( Gia_Man_t * p )                                  { int i; assert( p->vTiming != NULL ); Vec_FltFill(p->vTiming, 3*Gia_ManObjNum(p), 0); for ( i = 0; i < Gia_ManObjNum(p); i++ )  Vec_FltWriteEntry( p->vTiming, 3*i+1, (float)(ABC_INFINITY) ); }
static inline void         Gia_ManTimeStart( Gia_Man_t * p )                                  { assert( p->vTiming == NULL ); p->vTiming = Vec_FltAlloc(0); Gia_ManTimeClean( p );  }
static inline void         Gia_ManTimeStop( Gia_Man_t * p )                                   { assert( p->vTiming != NULL ); Vec_FltFreeP(&p->vTiming);                            }
static inline float        Gia_ObjTimeArrival( Gia_Man_t * p, int Id )                        { return Vec_FltEntry(p->vTiming, 3*Id+0);                                            }
static inline float        Gia_ObjTimeRequired( Gia_Man_t * p, int Id )                       { return Vec_FltEntry(p->vTiming, 3*Id+1);                                            }
static inline float        Gia_ObjTimeSlack( Gia_Man_t * p, int Id )                          { return Vec_FltEntry(p->vTiming, 3*Id+2);                                            }
static inline float        Gia_ObjTimeArrivalObj( Gia_Man_t * p, Gia_Obj_t * pObj )           { return Gia_ObjTimeArrival( p, Gia_ObjId(p, pObj) );                                 }
static inline float        Gia_ObjTimeRequiredObj( Gia_Man_t * p, Gia_Obj_t * pObj )          { return Gia_ObjTimeRequired( p, Gia_ObjId(p, pObj) );                                }
static inline float        Gia_ObjTimeSlackObj( Gia_Man_t * p, Gia_Obj_t * pObj )             { return Gia_ObjTimeSlack( p, Gia_ObjId(p, pObj) );                                   }
static inline void         Gia_ObjSetTimeArrival( Gia_Man_t * p, int Id, float t )            { Vec_FltWriteEntry( p->vTiming, 3*Id+0, t );                                         }
static inline void         Gia_ObjSetTimeRequired( Gia_Man_t * p, int Id, float t )           { Vec_FltWriteEntry( p->vTiming, 3*Id+1, t );                                         }
static inline void         Gia_ObjSetTimeSlack( Gia_Man_t * p, int Id, float t )              { Vec_FltWriteEntry( p->vTiming, 3*Id+2, t );                                         }
static inline void         Gia_ObjSetTimeArrivalObj( Gia_Man_t * p, Gia_Obj_t * pObj, float t )  { Gia_ObjSetTimeArrival( p, Gia_ObjId(p, pObj), t );                               }
static inline void         Gia_ObjSetTimeRequiredObj( Gia_Man_t * p, Gia_Obj_t * pObj, float t ) { Gia_ObjSetTimeRequired( p, Gia_ObjId(p, pObj), t );                              }
static inline void         Gia_ObjSetTimeSlackObj( Gia_Man_t * p, Gia_Obj_t * pObj, float t )    { Gia_ObjSetTimeSlack( p, Gia_ObjId(p, pObj), t );                                 }

static inline int          Gia_ObjSimWords( Gia_Man_t * p )                    { return Vec_WrdSize( p->vSimsPi ) / Gia_ManPiNum( p );          }
static inline word *       Gia_ObjSimPi( Gia_Man_t * p, int PiId )             { return Vec_WrdEntryP( p->vSimsPi, PiId * Gia_ObjSimWords(p) ); }
static inline word *       Gia_ObjSim( Gia_Man_t * p, int Id )                 { return Vec_WrdEntryP( p->vSims, Id * Gia_ObjSimWords(p) );     }
static inline word *       Gia_ObjSimObj( Gia_Man_t * p, Gia_Obj_t * pObj )    { return Gia_ObjSim( p, Gia_ObjId(p, pObj) );                    }

// AIG construction
extern void Gia_ObjAddFanout( Gia_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pFanout );
// THE SINGLE PLACE AN OBJECT IS BORN. Every constructor below reaches the array through this one
// helper, which hands back the next free slot after growing the array if it is full. What it does
// is worth reading closely, because three properties of the whole package come from these lines.
//  - Growth is by doubling, clamped at 1 << 29 objects. When that ceiling is actually reached the
//    function prints "Hard limit on the number of nodes (2^29) is reached. Quitting..." and calls
//    exit(1); it does not return an error to the caller, so a client cannot catch the condition.
//    The ceiling is not arbitrary: 1 << 29 is exactly the span of the 29-bit offset fields, so an
//    array any larger could hold a pair of objects whose distance the encoding cannot express.
//  - The realloc MOVES the array, and therefore every object in it. This is the reason fanins are
//    stored as distances: a distance between two slots is unchanged by the move, whereas a stored
//    address would be left dangling. The corollary for callers is unavoidable and unchecked - any
//    Gia_Obj_t * held across a call that may append is invalid afterwards, which is why the
//    identifier forms of the accessors exist and why side tables store literals, not addresses.
//    The freshly grown tail is memset to zero, so a slot reads as an object whose offsets are both
//    zero until a constructor writes it.
//  - pMuxes, when present, is reallocated and zeroed in the same breath. That is what keeps a side
//    array indexed by object identifier in step with pObjs; a side array that is not grown here
//    has to be extended by whoever owns it.
// The last line before the return pushes one zero onto p->vHash whenever the hash table is live,
// which is how the invariant Vec_IntSize(&p->vHash) == Gia_ManObjNum(p) that Gia_ManHashFind
// asserts (giaHash.c:L54-68) is maintained one object at a time. That push can itself reallocate
// vHash, which invalidates any interior int * into it: a SECOND and quite separate hazard from the
// object move above, and the reason the hashing layer re-runs its lookup after appending
// (giaHash.c:L497, L552, L609). The two must not be conflated.
static inline Gia_Obj_t * Gia_ManAppendObj( Gia_Man_t * p )  
{ 
    if ( p->nObjs == p->nObjsAlloc )
    {
        int nObjNew = Abc_MinInt( 2 * p->nObjsAlloc, (1 << 29) );
        if ( p->nObjs == (1 << 29) )
            printf( "Hard limit on the number of nodes (2^29) is reached. Quitting...\n" ), exit(1);
        assert( p->nObjs < nObjNew );
        if ( p->fVerbose )
            printf("Extending GIA object storage: %d -> %d.\n", p->nObjsAlloc, nObjNew );
        assert( p->nObjsAlloc > 0 );
        p->pObjs = ABC_REALLOC( Gia_Obj_t, p->pObjs, nObjNew );
        memset( p->pObjs + p->nObjsAlloc, 0, sizeof(Gia_Obj_t) * (nObjNew - p->nObjsAlloc) );
        if ( p->pMuxes )
        {
            p->pMuxes = ABC_REALLOC( unsigned, p->pMuxes, nObjNew );
            memset( p->pMuxes + p->nObjsAlloc, 0, sizeof(unsigned) * (nObjNew - p->nObjsAlloc) );
        }
        p->nObjsAlloc = nObjNew;
    }
    if ( Vec_IntSize(&p->vHTable) ) Vec_IntPush( &p->vHash, 0 );
    return Gia_ManObj( p, p->nObjs++ );
}
// THE CONSTRUCTORS. Seventeen static inline functions named Gia_ManAppend* live between here and
// Gia_ManPatchCoDriver below: this growth helper plus sixteen constructors. Only six of the
// sixteen write an object themselves - Ci, And, XorReal, MuxReal, Buf and Co, each of them calling
// Gia_ManAppendObj exactly once - and the other ten are wrappers that either compose several ANDs
// (the structural forms) or fold constants before delegating (the forms whose names end in 2).
// Two conventions hold across all sixteen. Each returns a LITERAL, always uncomplemented, formed
// as Gia_ObjId(p, pObj) << 1, so a caller wanting the inverted edge complements the result itself.
// And none of them consults or updates the structural hash table: canonicalization and sharing
// are the business of the Gia_ManHash* entry points in giaHash.c, which call these to do the
// actual writing. In particular Gia_ManAppendAnd does NO constant folding whatsoever; a reader who
// stops at it will wrongly conclude the package cannot fold, when in truth folding lives in the
// hashing layer (giaHash.c:L578-585) and in the 2-suffixed wrappers here.
// Read each constructor for two things: which fields it writes, and which ordering invariant it
// therefore establishes - the ordering being the only type tag the object has.
// Combinational input: sets fTerm, parks GIA_NONE in iDiff0 to mark "no fanin", and uses the
// terminal overload of iDiff1 to record this input's position in vCis, which it then appends to.
static inline int Gia_ManAppendCi( Gia_Man_t * p )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    pObj->fTerm = 1;
    pObj->iDiff0 = GIA_NONE;
    pObj->iDiff1 = Vec_IntSize( p->vCis );
    Vec_IntPush( p->vCis, Gia_ObjId(p, pObj) );
    return Gia_ObjId( p, pObj ) << 1;
}

extern void Gia_ManQuantSetSuppAnd( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void Gia_ManBuiltInSimPerform( Gia_Man_t * p, int iObj );

// Two-input AND, the workhorse. The branch below is the encoding's type tag being written: the two
// LITERALS are compared, and both branches fill the fields so that the SMALLER literal ends up in
// fanin 0 and the larger in fanin 1. Since an offset is the object identifier minus the fanin
// identifier, the smaller literal yields the LARGER offset. The distinct-fanin assertion just above
// guarantees the two variables differ, so a plain AND leaves iDiff0 > iDiff1 strictly - which is
// what Gia_ObjIsAndReal tests and what distinguishes it from a real XOR. Comparing literals rather
// than variables also means the polarity participates in the ordering, which is what makes the
// form canonical for the hash table: AND(a, !b) and AND(!b, a) produce identical objects.
// The distinct-fanin assertion is relaxed when p->fGiaSimple is set. With it relaxed, appending
// AND(x, x) writes equal offsets, and Gia_ObjIsBuf then reports that object as a buffer - an
// observed consequence of the encoding having no room for a separate tag, not a special case
// handled anywhere here.
// Four manager members, each tested simply for being set, add side effects to this one function, and
// each is worth knowing because it changes what a field means for the rest of the pass:
//  - p->pFanData: the object is linked into the dynamic fanout lists of both its fanins.
//  - p->fSweeper: each fanin's fMark0/fMark1 pair becomes a two-bit saturating fanout counter -
//    first fanout sets fMark0, any later one sets fMark1 - and fPhase is recomputed as the
//    conjunction of the two incoming EDGE values, each fanin's phase XORed with that edge's own
//    complement bit, rather than left as the all-zero-pattern value.
//  - p->fBuiltInSim: fPhase is recomputed by that same expression and the object is simulated
//    immediately through Gia_ManBuiltInSimPerform.
//  - p->vSuppWords: support information is extended for the new object by Gia_ManQuantSetSuppAnd.
// A pass that sets any of these owns the fields it touches for its whole duration.
static inline int Gia_ManAppendAnd( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    assert( iLit0 >= 0 && Abc_Lit2Var(iLit0) < Gia_ManObjNum(p) );
    assert( iLit1 >= 0 && Abc_Lit2Var(iLit1) < Gia_ManObjNum(p) );
    assert( p->fGiaSimple || Abc_Lit2Var(iLit0) != Abc_Lit2Var(iLit1) );
    if ( iLit0 < iLit1 )
    {
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit1));
    }
    else
    {
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit1));
    }
    if ( p->pFanData )
    {
        Gia_ObjAddFanout( p, Gia_ObjFanin0(pObj), pObj );
        Gia_ObjAddFanout( p, Gia_ObjFanin1(pObj), pObj );
    }
    if ( p->fSweeper )
    {
        Gia_Obj_t * pFan0 = Gia_ObjFanin0(pObj);
        Gia_Obj_t * pFan1 = Gia_ObjFanin1(pObj);
        if ( pFan0->fMark0 ) pFan0->fMark1 = 1; else pFan0->fMark0 = 1;
        if ( pFan1->fMark0 ) pFan1->fMark1 = 1; else pFan1->fMark0 = 1;
        pObj->fPhase = (Gia_ObjPhase(pFan0) ^ Gia_ObjFaninC0(pObj)) & (Gia_ObjPhase(pFan1) ^ Gia_ObjFaninC1(pObj));
    }
    if ( p->fBuiltInSim )
    {
        Gia_Obj_t * pFan0 = Gia_ObjFanin0(pObj);
        Gia_Obj_t * pFan1 = Gia_ObjFanin1(pObj);
        pObj->fPhase = (Gia_ObjPhase(pFan0) ^ Gia_ObjFaninC0(pObj)) & (Gia_ObjPhase(pFan1) ^ Gia_ObjFaninC1(pObj));
        Gia_ManBuiltInSimPerform( p, Gia_ObjId( p, pObj ) );
    }
    if ( p->vSuppWords )
        Gia_ManQuantSetSuppAnd( p, pObj );
    return Gia_ObjId( p, pObj ) << 1;
}
// REAL exclusive-or: one object holding an XOR outright, as opposed to the STRUCTURAL form built
// from several ANDs by Gia_ManAppendXor further down. The branch looks like the AND one but is not:
// it compares VARIABLES, not literals, and it fills the fields the opposite way round, so a real
// XOR always leaves iDiff0 < iDiff1. That inversion is the tag - it is the entirety of what
// Gia_ObjIsXor tests - and it is why the comparison must ignore polarity: an XOR absorbs the
// complement of either input into the output, so the two polarities of an input must not lead to
// two different objects. The polarity is still recorded in fCompl0/fCompl1 and the caller, not this
// function, is expected to have canonicalized it; the two assertions left commented out just below
// are where that check would have gone, and are recorded here unchanged.
// The counter p->nXors is what Gia_ManXorNum reports; nothing recomputes it from the objects.
static inline int Gia_ManAppendXorReal( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    assert( iLit0 >= 0 && Abc_Lit2Var(iLit0) < Gia_ManObjNum(p) );
    assert( iLit1 >= 0 && Abc_Lit2Var(iLit1) < Gia_ManObjNum(p) );
    assert( Abc_Lit2Var(iLit0) != Abc_Lit2Var(iLit1) );
    //assert( !Abc_LitIsCompl(iLit0) );
    //assert( !Abc_LitIsCompl(iLit1) );
    if ( Abc_Lit2Var(iLit0) > Abc_Lit2Var(iLit1) )
    {
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit1));
    }
    else
    {
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit1));
    }
    p->nXors++;
    return Gia_ObjId( p, pObj ) << 1;
}
// REAL multiplexer, and the one kind whose object carries no trace of what it is. The twelve bytes
// written here are indistinguishable from those of a real AND - the branch orders the two data
// literals by VARIABLE and leaves iDiff0 > iDiff1 whichever way it goes - and the entire distinction
// lives in the control literal parked in p->pMuxes at this object's identifier. That is why the
// function asserts pMuxes is already allocated, why Gia_ObjIsMux needs the manager, and why
// Gia_ObjIsAndReal has to exclude MUX-marked identifiers explicitly. Losing pMuxes turns every real
// MUX in the network into a real AND with no error reported anywhere.
// When the branch swaps the data literals it complements the control literal, because selecting
// between swapped data with the same control would compute the opposite function. The parameter
// order is (control, then-literal, else-literal), which is not the order they are stored in.
// The counter p->nMuxes is what Gia_ManMuxNum reports.
static inline int Gia_ManAppendMuxReal( Gia_Man_t * p, int iLitC, int iLit1, int iLit0 )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    assert( p->pMuxes != NULL );
    assert( iLit0 >= 0 && Abc_Lit2Var(iLit0) < Gia_ManObjNum(p) );
    assert( iLit1 >= 0 && Abc_Lit2Var(iLit1) < Gia_ManObjNum(p) );
    assert( iLitC >= 0 && Abc_Lit2Var(iLitC) < Gia_ManObjNum(p) );
    assert( Abc_Lit2Var(iLit0) != Abc_Lit2Var(iLit1) );
    assert( Abc_Lit2Var(iLitC) != Abc_Lit2Var(iLit0) );
    assert( Abc_Lit2Var(iLitC) != Abc_Lit2Var(iLit1) );
    assert( !Vec_IntSize(&p->vHTable) || !Abc_LitIsCompl(iLit1) );
    if ( Abc_Lit2Var(iLit0) < Abc_Lit2Var(iLit1) )
    {
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit1));
        p->pMuxes[Gia_ObjId(p, pObj)] = iLitC;
    }
    else
    {
        pObj->iDiff1  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0));
        pObj->fCompl1 = (unsigned)(Abc_LitIsCompl(iLit0));
        pObj->iDiff0  = (unsigned)(Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit1));
        pObj->fCompl0 = (unsigned)(Abc_LitIsCompl(iLit1));
        p->pMuxes[Gia_ObjId(p, pObj)] = Abc_LitNot(iLitC);
    }
    p->nMuxes++;
    return Gia_ObjId( p, pObj ) << 1;
}
// Buffer: one fanin written into BOTH offsets, which makes iDiff0 == iDiff1 and is precisely the
// condition Gia_ObjIsBuf tests. A buffer is thus not a separate kind in the encoding but an AND of
// an edge with itself, which is why Gia_ObjIsAnd is true of it and why Gia_ObjIsAndNotBuf exists
// at all. The complement bit is written into both fCompl fields as well, so an inverter is a buffer
// with both bits set. p->nBufs is what Gia_ManBufNum reports and what Gia_ManForEachBuf uses to
// decide whether iterating at all is worthwhile.
static inline int Gia_ManAppendBuf( Gia_Man_t * p, int iLit )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    assert( iLit >= 0 && Abc_Lit2Var(iLit) < Gia_ManObjNum(p) );
    pObj->iDiff0  = pObj->iDiff1  = Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit);
    pObj->fCompl0 = pObj->fCompl1 = Abc_LitIsCompl(iLit);
    p->nBufs++;
    return Gia_ObjId( p, pObj ) << 1;
}
// Combinational output. Note the order: the driver literal is checked BEFORE the object is
// appended, because the second assertion asks whether the driver is itself a combinational output
// and appending first would let the new slot be inspected. fTerm is set, iDiff0 holds the single
// fanin offset - which is what makes Gia_ObjIsCo the complement of Gia_ObjIsCi at the same fTerm -
// and iDiff1 takes the terminal overload again, recording this output's position in vCos.
static inline int Gia_ManAppendCo( Gia_Man_t * p, int iLit0 )  
{ 
    Gia_Obj_t * pObj;
    assert( iLit0 >= 0 && Abc_Lit2Var(iLit0) < Gia_ManObjNum(p) );
    assert( !Gia_ObjIsCo(Gia_ManObj(p, Abc_Lit2Var(iLit0))) );
    pObj = Gia_ManAppendObj( p );    
    pObj->fTerm = 1;
    pObj->iDiff0  = Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit0);
    pObj->fCompl0 = Abc_LitIsCompl(iLit0);
    pObj->iDiff1  = Vec_IntSize( p->vCos );
    Vec_IntPush( p->vCos, Gia_ObjId(p, pObj) );
    if ( p->pFanData )
        Gia_ObjAddFanout( p, Gia_ObjFanin0(pObj), pObj );
    return Gia_ObjId( p, pObj ) << 1;
}
// THE STRUCTURAL FORMS. These four add no new kind of object: they express their function in terms
// of ANDs and inverted edges, so each call adds one object for Or, three for Mux, four for Maj and
// three for Xor - the last by delegating to Mux. Compare them with the REAL forms above, which put
// the same function in a single object: choosing the structural form keeps the network a pure AIG
// that any pass can read, while choosing the real form keeps it smaller but requires every consumer
// to handle XOR and MUX objects. Neither folds constants, so passing a constant literal here builds
// the gate anyway; the 2-suffixed wrappers below are the folding versions.
static inline int Gia_ManAppendOr( Gia_Man_t * p, int iLit0, int iLit1 )
{
    return Abc_LitNot(Gia_ManAppendAnd( p, Abc_LitNot(iLit0), Abc_LitNot(iLit1) ));
}
static inline int Gia_ManAppendMux( Gia_Man_t * p, int iCtrl, int iData1, int iData0 )  
{ 
    int iTemp0 = Gia_ManAppendAnd( p, Abc_LitNot(iCtrl), iData0 );
    int iTemp1 = Gia_ManAppendAnd( p, iCtrl, iData1 );
    return Abc_LitNotCond( Gia_ManAppendAnd( p, Abc_LitNot(iTemp0), Abc_LitNot(iTemp1) ), 1 );
}
static inline int Gia_ManAppendMaj( Gia_Man_t * p, int iData0, int iData1, int iData2 )  
{ 
    int iTemp0 = Gia_ManAppendOr( p, iData1, iData2 );
    int iTemp1 = Gia_ManAppendAnd( p, iData0, iTemp0 );
    int iTemp2 = Gia_ManAppendAnd( p, iData1, iData2 );
    return Gia_ManAppendOr( p, iTemp1, iTemp2 );
}
static inline int Gia_ManAppendXor( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    return Gia_ManAppendMux( p, iLit0, Abc_LitNot(iLit1), iLit1 );
}

// THE FOLDING WRAPPERS. Every name ending in 2 is the constant-folding counterpart of the plain
// form above it, and this is the ONLY place in this file where folding happens - Gia_ManAppendAnd
// itself never inspects its arguments for constants. Gia_ManAppendAnd2 tests for four cases that
// need no object at all - a constant in either argument position, which accounts for two of the
// tests, then identical arguments, then complementary arguments. Or2, Mux2, Maj2 and Xor2 reach
// exactly those recognitions by being built from it, while Gia_ManAppendXorReal2 carries its own
// four tests of the same shape but with XOR results - identical arguments fold to 0 and
// complementary ones to 1 - before delegating to Gia_ManAppendXorReal. These wrappers are not the
// hashing layer: they can decline to create an object, but they never find an existing one, so two
// separate calls with the same inputs still produce two objects. Sharing requires Gia_ManHashAnd.
// All of the folding is disabled when p->fGiaSimple is set, which is what makes that flag mean
// "record exactly the gates I ask for" - and which is also how a buffer-shaped AND can arise, as
// noted at Gia_ManAppendAnd above.
static inline int Gia_ManAppendAnd2( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    if ( !p->fGiaSimple )
    {
        if ( iLit0 < 2 )
            return iLit0 ? iLit1 : 0;
        if ( iLit1 < 2 )
            return iLit1 ? iLit0 : 0;
        if ( iLit0 == iLit1 )
            return iLit1;
        if ( iLit0 == Abc_LitNot(iLit1) )
            return 0;
    }
    return Gia_ManAppendAnd( p, iLit0, iLit1 );
}
static inline int Gia_ManAppendOr2( Gia_Man_t * p, int iLit0, int iLit1 )
{
    return Abc_LitNot(Gia_ManAppendAnd2( p, Abc_LitNot(iLit0), Abc_LitNot(iLit1) ));
}
static inline int Gia_ManAppendMux2( Gia_Man_t * p, int iCtrl, int iData1, int iData0 )  
{ 
    int iTemp0 = Gia_ManAppendAnd2( p, Abc_LitNot(iCtrl), iData0 );
    int iTemp1 = Gia_ManAppendAnd2( p, iCtrl, iData1 );
    return Abc_LitNotCond( Gia_ManAppendAnd2( p, Abc_LitNot(iTemp0), Abc_LitNot(iTemp1) ), 1 );
}
static inline int Gia_ManAppendMaj2( Gia_Man_t * p, int iData0, int iData1, int iData2 )  
{ 
    int iTemp0 = Gia_ManAppendOr2( p, iData1, iData2 );
    int iTemp1 = Gia_ManAppendAnd2( p, iData0, iTemp0 );
    int iTemp2 = Gia_ManAppendAnd2( p, iData1, iData2 );
    return Gia_ManAppendOr2( p, iTemp1, iTemp2 );
}
static inline int Gia_ManAppendXor2( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    return Gia_ManAppendMux2( p, iLit0, Abc_LitNot(iLit1), iLit1 );
}

static inline int Gia_ManAppendXorReal2( Gia_Man_t * p, int iLit0, int iLit1 )  
{ 
    if ( !p->fGiaSimple )
    {
        if ( iLit0 < 2 )
            return iLit0 ? Abc_LitNot(iLit1) : iLit1;
        if ( iLit1 < 2 )
            return iLit1 ? Abc_LitNot(iLit0) : iLit0;
        if ( iLit0 == iLit1 )
            return 0;
        if ( iLit0 == Abc_LitNot(iLit1) )
            return 1;
    }
    return Gia_ManAppendXorReal( p, iLit0, iLit1 );
}

// Rewiring an existing output in place, the one function here that alters an object already built.
// It is confined to combinational outputs, and its assertion is the encoding's own rule restated:
// the new driver must have a strictly smaller identifier than the output, because a backward offset
// cannot reach forward. Nothing here updates fanout data, reference counts or the hash table, so a
// caller that keeps any of those has to refresh them itself.
static inline void Gia_ManPatchCoDriver( Gia_Man_t * p, int iCoIndex, int iLit0 )  
{
    Gia_Obj_t * pObjCo  = Gia_ManCo( p, iCoIndex );
    assert( Gia_ObjId(p, pObjCo) > Abc_Lit2Var(iLit0) );
    pObjCo->iDiff0  = Gia_ObjId(p, pObjCo) - Abc_Lit2Var(iLit0);
    pObjCo->fCompl0 = Abc_LitIsCompl(iLit0);
}

// TERNARY SIMULATION. Three of the four values of a two-bit code, used where a signal may be
// unknown as well as 0 or 1; the fourth code, 0, means "no value assigned yet" and is written by
// Gia_ObjTerSimSetC below. The two helpers that follow are the ternary counterparts of inversion
// and conjunction: Gia_XsimNotCond keeps GIA_UND unknown and otherwise flips against the edge's
// complement bit, and Gia_XsimAndCond checks for a controlling zero on either input FIRST, so that
// 0 AND unknown is 0 rather than unknown, which is what makes the simulation as strong as it can be
// without case splitting.
#define GIA_ZER 1
#define GIA_ONE 2
#define GIA_UND 3

static inline int Gia_XsimNotCond( int Value, int fCompl )   
{ 
    if ( Value == GIA_UND )
        return GIA_UND;
    if ( Value == GIA_ZER + fCompl )
        return GIA_ZER;
    return GIA_ONE;
}
static inline int Gia_XsimAndCond( int Value0, int fCompl0, int Value1, int fCompl1 )   
{ 
    if ( Value0 == GIA_ZER + fCompl0 || Value1 == GIA_ZER + fCompl1 )
        return GIA_ZER;
    if ( Value0 == GIA_UND || Value1 == GIA_UND )
        return GIA_UND;
    return GIA_ONE;
}


// Where that ternary value is kept: NOT in a field of its own, but packed into the fMark0/fMark1
// pair, one bit each. This is a second and wholly incompatible meaning for those two bits - the
// first being the generic user mark, and two more arriving with p->fSweeper's fanout counter and
// with the delete marker Gia_ManDupMarked reads (giaDup.c:L1472-1476). A pass that runs ternary
// simulation therefore owns both marks for its entire duration, and any other use of them in the
// same pass corrupts the values silently: nothing here records which meaning is currently in force.
// The four setters write the code and the four getters test it, so the packing itself never appears
// in calling code; Gia_ObjTerSimSetC writes the all-zero code, which the getters read as "no value".
static inline void Gia_ObjTerSimSetC( Gia_Obj_t * pObj ) { pObj->fMark0 = 0; pObj->fMark1 = 0;    }
static inline void Gia_ObjTerSimSet0( Gia_Obj_t * pObj ) { pObj->fMark0 = 1; pObj->fMark1 = 0;    }
static inline void Gia_ObjTerSimSet1( Gia_Obj_t * pObj ) { pObj->fMark0 = 0; pObj->fMark1 = 1;    }
static inline void Gia_ObjTerSimSetX( Gia_Obj_t * pObj ) { pObj->fMark0 = 1; pObj->fMark1 = 1;    }

static inline int  Gia_ObjTerSimGetC( Gia_Obj_t * pObj ) { return !pObj->fMark0 && !pObj->fMark1; }
static inline int  Gia_ObjTerSimGet0( Gia_Obj_t * pObj ) { return  pObj->fMark0 && !pObj->fMark1; }
static inline int  Gia_ObjTerSimGet1( Gia_Obj_t * pObj ) { return !pObj->fMark0 &&  pObj->fMark1; }
static inline int  Gia_ObjTerSimGetX( Gia_Obj_t * pObj ) { return  pObj->fMark0 &&  pObj->fMark1; }

static inline int  Gia_ObjTerSimGet0Fanin0( Gia_Obj_t * pObj ) { return (Gia_ObjTerSimGet1(Gia_ObjFanin0(pObj)) && Gia_ObjFaninC0(pObj)) || (Gia_ObjTerSimGet0(Gia_ObjFanin0(pObj)) && !Gia_ObjFaninC0(pObj)); }
static inline int  Gia_ObjTerSimGet1Fanin0( Gia_Obj_t * pObj ) { return (Gia_ObjTerSimGet0(Gia_ObjFanin0(pObj)) && Gia_ObjFaninC0(pObj)) || (Gia_ObjTerSimGet1(Gia_ObjFanin0(pObj)) && !Gia_ObjFaninC0(pObj)); }

static inline int  Gia_ObjTerSimGet0Fanin1( Gia_Obj_t * pObj ) { return (Gia_ObjTerSimGet1(Gia_ObjFanin1(pObj)) && Gia_ObjFaninC1(pObj)) || (Gia_ObjTerSimGet0(Gia_ObjFanin1(pObj)) && !Gia_ObjFaninC1(pObj)); }
static inline int  Gia_ObjTerSimGet1Fanin1( Gia_Obj_t * pObj ) { return (Gia_ObjTerSimGet0(Gia_ObjFanin1(pObj)) && Gia_ObjFaninC1(pObj)) || (Gia_ObjTerSimGet1(Gia_ObjFanin1(pObj)) && !Gia_ObjFaninC1(pObj)); }

static inline void Gia_ObjTerSimAnd( Gia_Obj_t * pObj )
{
    assert( Gia_ObjIsAnd(pObj) );
    assert( !Gia_ObjTerSimGetC( Gia_ObjFanin0(pObj) ) );
    assert( !Gia_ObjTerSimGetC( Gia_ObjFanin1(pObj) ) );
    if ( Gia_ObjTerSimGet0Fanin0(pObj) || Gia_ObjTerSimGet0Fanin1(pObj) )
        Gia_ObjTerSimSet0( pObj );
    else if ( Gia_ObjTerSimGet1Fanin0(pObj) && Gia_ObjTerSimGet1Fanin1(pObj) )
        Gia_ObjTerSimSet1( pObj );
    else 
        Gia_ObjTerSimSetX( pObj );
}
static inline void Gia_ObjTerSimCo( Gia_Obj_t * pObj )
{
    assert( Gia_ObjIsCo(pObj) );
    assert( !Gia_ObjTerSimGetC( Gia_ObjFanin0(pObj) ) );
    if ( Gia_ObjTerSimGet0Fanin0(pObj) )
        Gia_ObjTerSimSet0( pObj );
    else if ( Gia_ObjTerSimGet1Fanin0(pObj) )
        Gia_ObjTerSimSet1( pObj );
    else 
        Gia_ObjTerSimSetX( pObj );
}
static inline void Gia_ObjTerSimRo( Gia_Man_t * p, Gia_Obj_t * pObj )
{
    Gia_Obj_t * pTemp = Gia_ObjRoToRi(p, pObj);
    assert( Gia_ObjIsRo(p, pObj) );
    assert( !Gia_ObjTerSimGetC( pTemp ) );
    pObj->fMark0 = pTemp->fMark0;
    pObj->fMark1 = pTemp->fMark1;
}

static inline void Gia_ObjTerSimPrint( Gia_Obj_t * pObj )
{
    if ( Gia_ObjTerSimGet0(pObj) )
        printf( "0" );
    else if ( Gia_ObjTerSimGet1(pObj) )
        printf( "1" );
    else if ( Gia_ObjTerSimGetX(pObj) )
        printf( "X" );
}

static inline int Gia_AigerReadInt( unsigned char * pPos )
{
    int i, Value = 0;
    for ( i = 0; i < 4; i++ )
        Value = (Value << 8) | *pPos++;
    return Value;
}
static inline void Gia_AigerWriteInt( unsigned char * pPos, int Value )
{
    int i;
    for ( i = 3; i >= 0; i-- )
        *pPos++ = (Value >> (8*i)) & 255;
}
static inline unsigned Gia_AigerReadUnsigned( unsigned char ** ppPos )
{
    unsigned x = 0, i = 0;
    unsigned char ch;
    while ((ch = *(*ppPos)++) & 0x80)
        x |= (ch & 0x7f) << (7 * i++);
    return x | (ch << (7 * i));
}
static inline void Gia_AigerWriteUnsigned( Vec_Str_t * vStr, unsigned x )
{
    unsigned char ch;
    while (x & ~0x7f)
    {
        ch = (x & 0x7f) | 0x80;
        Vec_StrPush( vStr, ch );
        x >>= 7;
    }
    ch = x;
    Vec_StrPush( vStr, ch );
}
static inline void Gia_AigerWriteUnsignedFile( FILE * pFile, unsigned x )
{
    unsigned char ch;
    while (x & ~0x7f)
    {
        ch = (x & 0x7f) | 0x80;
        fputc( ch, pFile );
        x >>= 7;
    }
    ch = x;
    fputc( ch, pFile );
}
static inline int Gia_AigerWriteUnsignedBuffer( unsigned char * pBuffer, int Pos, unsigned x )
{
    unsigned char ch;
    while (x & ~0x7f)
    {
        ch = (x & 0x7f) | 0x80;
        pBuffer[Pos++] = ch;
        x >>= 7;
    }
    ch = x;
    pBuffer[Pos++] = ch;
    return Pos;
}

// EQUIVALENCE CLASSES AND CHOICES. Two side arrays, both indexed by object identifier, encode the
// candidate equivalences an equivalence-checking or sweeping pass has found: pReprs holds one
// Gia_Rpr_t per object naming that object's class REPRESENTATIVE, and pNexts holds one int per
// object threading the members of a class into a singly linked list. A class is therefore a chain
// whose head is the representative and whose links are identifiers, terminated by 0 - which is a
// legal terminator only because object 0 is the constant and can never be a class member.
// THE SENTINEL HERE IS GIA_VOID, NOT GIA_NONE. Everything in this block that means "no
// representative" compares against GIA_VOID, because iRepr is 28 bits wide while the offset fields
// are 29; the two constants are not interchangeable and using the wrong one silently misclassifies.
// Gia_ObjSetRepr asserts the representative has a SMALLER identifier than the member, so class
// chains run in the same direction as the fanin offsets; Gia_ObjSetReprRev is the deliberate
// exception with the assertion reversed, for passes that build classes the other way round.
// Class membership is read as four mutually exclusive states, all derived from the two arrays
// rather than stored: a representative is a head (no repr of its own, but a next), an object with a
// representative and no next is a tail, an object with neither is none, and identifier 0's repr
// being 0 marks the constant class. pSibls is a separate array for structural choices, and
// Gia_ObjSibl returns 0 rather than asserting when the manager carries none.
static inline Gia_Obj_t * Gia_ObjReprObj( Gia_Man_t * p, int Id )            { return p->pReprs[Id].iRepr == GIA_VOID ? NULL : Gia_ManObj( p, p->pReprs[Id].iRepr );                  }
static inline int         Gia_ObjRepr( Gia_Man_t * p, int Id )               { return p->pReprs[Id].iRepr;                                                }
static inline void        Gia_ObjSetRepr( Gia_Man_t * p, int Id, int Num )   { assert( Num == GIA_VOID || Num < Id ); p->pReprs[Id].iRepr = Num;          }
static inline void        Gia_ObjSetReprRev( Gia_Man_t * p, int Id, int Num ){ assert( Num == GIA_VOID || Num > Id ); p->pReprs[Id].iRepr = Num;          }
static inline void        Gia_ObjUnsetRepr( Gia_Man_t * p, int Id )          { p->pReprs[Id].iRepr = GIA_VOID;                                            }
static inline int         Gia_ObjHasRepr( Gia_Man_t * p, int Id )            { return p->pReprs[Id].iRepr != GIA_VOID;                                    }
static inline int         Gia_ObjReprSelf( Gia_Man_t * p, int Id )           { return Gia_ObjHasRepr(p, Id) ? Gia_ObjRepr(p, Id) : Id;                    }
static inline int         Gia_ObjSibl( Gia_Man_t * p, int Id )               { return p->pSibls ? p->pSibls[Id] : 0;                                      }
static inline Gia_Obj_t * Gia_ObjSiblObj( Gia_Man_t * p, int Id )            { return (p->pSibls && p->pSibls[Id]) ? Gia_ManObj(p, p->pSibls[Id]) : NULL; }

static inline int         Gia_ObjProved( Gia_Man_t * p, int Id )             { return p->pReprs[Id].fProved;       }
static inline void        Gia_ObjSetProved( Gia_Man_t * p, int Id )          { p->pReprs[Id].fProved = 1;          }
static inline void        Gia_ObjUnsetProved( Gia_Man_t * p, int Id )        { p->pReprs[Id].fProved = 0;          }

static inline int         Gia_ObjFailed( Gia_Man_t * p, int Id )             { return p->pReprs[Id].fFailed;       }
static inline void        Gia_ObjSetFailed( Gia_Man_t * p, int Id )          { p->pReprs[Id].fFailed = 1;          }

static inline int         Gia_ObjColor( Gia_Man_t * p, int Id, int c )       { return c? p->pReprs[Id].fColorB : p->pReprs[Id].fColorA;          }
static inline int         Gia_ObjColors( Gia_Man_t * p, int Id )             { return p->pReprs[Id].fColorB * 2 + p->pReprs[Id].fColorA;         }
static inline void        Gia_ObjSetColor( Gia_Man_t * p, int Id, int c )    { if (c) p->pReprs[Id].fColorB = 1; else p->pReprs[Id].fColorA = 1; }
static inline void        Gia_ObjSetColors( Gia_Man_t * p, int Id )          { p->pReprs[Id].fColorB = p->pReprs[Id].fColorA = 1;                }
static inline int         Gia_ObjVisitColor( Gia_Man_t * p, int Id, int c )  { int x; if (c) { x = p->pReprs[Id].fColorB; p->pReprs[Id].fColorB = 1; } else { x = p->pReprs[Id].fColorA; p->pReprs[Id].fColorA = 1; } return x; }
static inline int         Gia_ObjDiffColors( Gia_Man_t * p, int i, int j )   { return (p->pReprs[i].fColorA ^ p->pReprs[j].fColorA) && (p->pReprs[i].fColorB ^ p->pReprs[j].fColorB); }
static inline int         Gia_ObjDiffColors2( Gia_Man_t * p, int i, int j )  { return (p->pReprs[i].fColorA ^ p->pReprs[j].fColorA) || (p->pReprs[i].fColorB ^ p->pReprs[j].fColorB); }

static inline Gia_Obj_t * Gia_ObjNextObj( Gia_Man_t * p, int Id )            { return p->pNexts[Id] == 0 ? NULL : Gia_ManObj( p, p->pNexts[Id] );}
static inline int         Gia_ObjNext( Gia_Man_t * p, int Id )               { return p->pNexts[Id];                                             }
static inline void        Gia_ObjSetNext( Gia_Man_t * p, int Id, int Num )   { p->pNexts[Id] = Num;                                              }

static inline int         Gia_ObjIsConst( Gia_Man_t * p, int Id )            { return Gia_ObjRepr(p, Id) == 0;                                   }
static inline int         Gia_ObjIsHead( Gia_Man_t * p, int Id )             { return Gia_ObjRepr(p, Id) == GIA_VOID && Gia_ObjNext(p, Id) > 0;  }
static inline int         Gia_ObjIsNone( Gia_Man_t * p, int Id )             { return Gia_ObjRepr(p, Id) == GIA_VOID && Gia_ObjNext(p, Id) <= 0; }
static inline int         Gia_ObjIsTail( Gia_Man_t * p, int Id )             { return (Gia_ObjRepr(p, Id) > 0 && Gia_ObjRepr(p, Id) != GIA_VOID) && Gia_ObjNext(p, Id) <= 0;                  }
static inline int         Gia_ObjIsClass( Gia_Man_t * p, int Id )            { return (Gia_ObjRepr(p, Id) > 0 && Gia_ObjRepr(p, Id) != GIA_VOID) || Gia_ObjNext(p, Id) > 0;                   }
static inline int         Gia_ObjHasSameRepr( Gia_Man_t * p, int i, int k )  { assert( k ); return i? (Gia_ObjRepr(p, i) == Gia_ObjRepr(p, k) && Gia_ObjRepr(p, i) != GIA_VOID) : Gia_ObjRepr(p, k) == 0;  }
static inline int         Gia_ObjIsFailedPair( Gia_Man_t * p, int i, int k ) { assert( k ); return i? (Gia_ObjFailed(p, i) || Gia_ObjFailed(p, k)) : Gia_ObjFailed(p, k);                     }
static inline int         Gia_ClassIsPair( Gia_Man_t * p, int i )            { assert( Gia_ObjIsHead(p, i) ); assert( Gia_ObjNext(p, i) ); return Gia_ObjNext(p, Gia_ObjNext(p, i)) <= 0;     }
static inline void        Gia_ClassUndoPair( Gia_Man_t * p, int i )          { assert( Gia_ClassIsPair(p,i) ); Gia_ObjSetRepr(p, Gia_ObjNext(p, i), GIA_VOID); Gia_ObjSetNext(p, i, 0);       }

// ITERATORS, PART ONE: the class iterators. GIA's iterators are macros that expand to the header of
// a for loop, so the statement or block a caller writes after the macro becomes the loop body. The
// filtering ones - every macro here whose name selects a subset - end in the idiom
//     if ( !predicate ) {} else
// which looks odd but is exactly right: it makes the caller's following statement the else branch,
// so non-matching objects are skipped without the macro needing to know anything about the body, and
// a trailing semicolon or a braced block both work. It also means a caller must not put an else of
// their own directly after such a loop, since it would bind to this if.
// Reading these four: ForEachConst walks the members of the constant class, ForEachClass the class
// heads from identifier 1, ForEachClass0 the same from 0 so the constant class is included, and
// ForEachClassReverse the heads downwards, stopping before 0. The three ClassForEachObj forms then
// walk one class by following pNexts, and each begins with an assertion inside the for initializer -
// an unusual but deliberate placement that makes the precondition part of the expansion: the
// starting identifier must be a class head.
#define Gia_ManForEachConst( p, i )                            \
    for ( i = 1; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsConst(p, i) ) {} else
#define Gia_ManForEachClass( p, i )                            \
    for ( i = 1; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsHead(p, i) ) {} else
#define Gia_ManForEachClass0( p, i )                            \
    for ( i = 0; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsHead(p, i) ) {} else
#define Gia_ManForEachClassReverse( p, i )                     \
    for ( i = Gia_ManObjNum(p) - 1; i > 0; i-- ) if ( !Gia_ObjIsHead(p, i) ) {} else
#define Gia_ClassForEachObj( p, i, iObj )                      \
    for ( assert(Gia_ObjIsHead(p, i) && i), iObj = i; iObj > 0; iObj = Gia_ObjNext(p, iObj) )
#define Gia_ClassForEachObj1( p, i, iObj )                     \
    for ( assert(Gia_ObjIsHead(p, i)), iObj = Gia_ObjNext(p, i); iObj > 0; iObj = Gia_ObjNext(p, iObj) )
#define Gia_ClassForEachObjStart( p, i, iObj, Start )          \
    for ( assert(Gia_ObjIsHead(p, i)), iObj = Gia_ObjNext(p, Start); iObj > 0; iObj = Gia_ObjNext(p, iObj) )


// STATIC FANOUT: one vector doing two jobs. p->vFanout is SELF-INDEXING - its first Gia_ManObjNum
// entries are offsets INTO THE SAME VECTOR, and the fanout identifiers of every object are stored
// past that header. So Gia_ObjFoffsetId reads object Id's offset, and Gia_ObjFanoutId adds the
// wanted position to it and reads again from the same array; the count comes from a second vector,
// p->vFanoutNums. Packing the lists back-to-back after their own index table is what makes this a
// single allocation instead of one list per object, and it is why the structure is called static:
// the lists are laid out once for a fixed network and cannot absorb a new object, unlike the
// pFanData lists that Gia_ManAppendAnd maintains incrementally.
// Nothing in this block validates that the vector has been built. On a manager without static
// fanout, Vec_IntEntry on an empty vector is what reports the problem, not an assertion here.
static inline int         Gia_ObjFoffsetId( Gia_Man_t * p, int Id )                { return Vec_IntEntry( p->vFanout, Id );                                 }
static inline int         Gia_ObjFoffset( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ObjFoffsetId( p, Gia_ObjId(p, pObj) );                      }
static inline int         Gia_ObjFanoutNumId( Gia_Man_t * p, int Id )              { return Vec_IntEntry( p->vFanoutNums, Id );                             }
static inline int         Gia_ObjFanoutNum( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjFanoutNumId( p, Gia_ObjId(p, pObj) );                    }
static inline int         Gia_ObjFanoutId( Gia_Man_t * p, int Id, int i )          { return Vec_IntEntry( p->vFanout, Gia_ObjFoffsetId(p, Id) + i );        }
static inline Gia_Obj_t * Gia_ObjFanout0( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ManObj( p, Gia_ObjFanoutId(p, Gia_ObjId(p, pObj), 0) );     }
static inline Gia_Obj_t * Gia_ObjFanout( Gia_Man_t * p, Gia_Obj_t * pObj, int i )  { return Gia_ManObj( p, Gia_ObjFanoutId(p, Gia_ObjId(p, pObj), i) );     }
static inline void        Gia_ObjSetFanout( Gia_Man_t * p, Gia_Obj_t * pObj, int i, Gia_Obj_t * pFan )   { Vec_IntWriteEntry( p->vFanout, Gia_ObjFoffset(p, pObj) + i, Gia_ObjId(p, pFan) ); }
static inline void        Gia_ObjSetFanoutInt( Gia_Man_t * p, Gia_Obj_t * pObj, int i, int x )           { Vec_IntWriteEntry( p->vFanout, Gia_ObjFoffset(p, pObj) + i, x );                  }

// The static-fanout iterators. The first two differ only in what they hand the body, an object or an
// identifier, and both use the (expression, 1) comma trick so that the assignment cannot terminate
// the loop by evaluating to zero - which matters because a fanout identifier is never 0, but an
// object address cast to int could be anything.
// The third macro, Gia_ObjForEachFanoutStaticIndex, has no caller anywhere in src/ at this commit: a
// tree-wide search finds only this definition. Recorded, not altered. Two observations about it, both
// read straight off the expansion below. Its middle guard includes (Index = Vec_IntEntry(p->vFanout,
// Id)+i) without the comma-and-1 wrapper the other two use, so the loop would stop early if that
// computed index were ever 0. And it indexes p->vFanout by Id and adds i, which is the OFFSET header
// entry plus a position rather than an entry of the fanout list itself, so Index is the position of
// the fanout in the vector rather than its identifier - which is what makes the following
// Vec_IntEntry(p->vFanout, Index) yield the identifier.
#define Gia_ObjForEachFanoutStatic( p, pObj, pFanout, i )         \
    for ( i = 0; (i < Gia_ObjFanoutNum(p, pObj)) && (((pFanout) = Gia_ObjFanout(p, pObj, i)), 1); i++ )
#define Gia_ObjForEachFanoutStaticId( p, Id, FanId, i )           \
    for ( i = 0; (i < Gia_ObjFanoutNumId(p, Id)) && ((FanId = Gia_ObjFanoutId(p, Id, i)), 1); i++ )
#define Gia_ObjForEachFanoutStaticIndex( p, Id, FanId, i, Index ) \
    for ( i = 0; (i < Gia_ObjFanoutNumId(p, Id)) && (Index = Vec_IntEntry(p->vFanout, Id)+i) && ((FanId = Vec_IntEntry(p->vFanout, Index)), 1); i++ )

// MAPPING: three independent representations of "this network has been covered by LUTs or cells".
// The first, p->vMapping, uses the same self-indexing layout as the static-fanout vector above: the
// first Gia_ManObjNum entries are offsets into the same vector, and each LUT record found at such an
// offset is a size followed by that many fanin identifiers. Hence the double indirection in
// Gia_ObjLutSize and the +1 in Gia_ObjLutFanins. An offset of 0 means "not a LUT", which is
// unambiguous because position 0 lies inside the header where no record can start. One entry past
// the fanins holds a cell identifier, which Gia_ObjLutMuxId reads and Gia_ObjLutIsMux tests for
// being negative, so a mapped MUX is distinguished by a sign rather than by a separate field.
// The second, p->vMapping2, is a Vec_Wec_t: one Vec_Int_t of fanins per object, addressed directly.
// It costs more memory than the packed form but can be edited in place, and p->vFanouts2 is its
// fanout counterpart. The two mappings are alternatives, and Gia_ManHasMapping and
// Gia_ManHasMapping2 are how a pass asks which one it has been handed.
// The third, p->vCellMapping, is indexed by LITERAL rather than by object identifier - which is why
// Gia_ManForEachCell below runs from 2 to twice the object count - so a node and its complement can
// carry different cells. Two negative entries are reserved as markers: -1 for an inverter and -2 for
// a buffer, tested by Gia_ObjIsCellInv and Gia_ObjIsCellBuf; anything else positive is an offset in
// the same self-indexing style as vMapping.
static inline int         Gia_ManHasMapping( Gia_Man_t * p )                { return p->vMapping != NULL;                                                   }
static inline int         Gia_ObjIsLut( Gia_Man_t * p, int Id )             { return Vec_IntEntry(p->vMapping, Id) != 0;                                    }
static inline int         Gia_ObjLutSize( Gia_Man_t * p, int Id )           { return Vec_IntEntry(p->vMapping, Vec_IntEntry(p->vMapping, Id));              }
static inline int *       Gia_ObjLutFanins( Gia_Man_t * p, int Id )         { return Vec_IntEntryP(p->vMapping, Vec_IntEntry(p->vMapping, Id)) + 1;         }
static inline int         Gia_ObjLutFanin( Gia_Man_t * p, int Id, int i )   { return Gia_ObjLutFanins(p, Id)[i];                                            }
static inline int         Gia_ObjLutMuxId( Gia_Man_t * p, int Id )          { return Gia_ObjLutFanins(p, Id)[Gia_ObjLutSize(p, Id)];                        }
static inline int         Gia_ObjLutIsMux( Gia_Man_t * p, int Id )          { return (int)(Gia_ObjLutMuxId(p, Id) < 0);                                     }

static inline int         Gia_ManHasMapping2( Gia_Man_t * p )               { return p->vMapping2 != NULL;                                                  }
static inline int         Gia_ObjIsLut2( Gia_Man_t * p, int Id )            { return Vec_IntSize(Vec_WecEntry(p->vMapping2, Id)) != 0;                      }
static inline int         Gia_ObjLutSize2( Gia_Man_t * p, int Id )          { return Vec_IntSize(Vec_WecEntry(p->vMapping2, Id));                           }
static inline Vec_Int_t * Gia_ObjLutFanins2( Gia_Man_t * p, int Id )        { return Vec_WecEntry(p->vMapping2, Id);                                        }
static inline int         Gia_ObjLutFanin2( Gia_Man_t * p, int Id, int i )  { return Vec_IntEntry(Vec_WecEntry(p->vMapping2, Id), i);                       }
static inline int         Gia_ObjLutFanoutNum2( Gia_Man_t * p, int Id )     { return Vec_IntSize(Vec_WecEntry(p->vFanouts2, Id));                           }
static inline int         Gia_ObjLutFanout2( Gia_Man_t * p, int Id, int i ) { return Vec_IntEntry(Vec_WecEntry(p->vFanouts2, Id), i);                       }

static inline int         Gia_ManHasCellMapping( Gia_Man_t * p )            { return p->vCellMapping != NULL;                                               }
static inline int         Gia_ObjIsCell( Gia_Man_t * p, int iLit )          { return Vec_IntEntry(p->vCellMapping, iLit) != 0;                              }
static inline int         Gia_ObjIsCellInv( Gia_Man_t * p, int iLit )       { return Vec_IntEntry(p->vCellMapping, iLit) == -1;                             }
static inline int         Gia_ObjIsCellBuf( Gia_Man_t * p, int iLit )       { return Vec_IntEntry(p->vCellMapping, iLit) == -2;                             }
static inline int         Gia_ObjCellSize( Gia_Man_t * p, int iLit )        { return Vec_IntEntry(p->vCellMapping, Vec_IntEntry(p->vCellMapping, iLit));    }
static inline int *       Gia_ObjCellFanins( Gia_Man_t * p, int iLit )      { return Vec_IntEntryP(p->vCellMapping, Vec_IntEntry(p->vCellMapping, iLit))+1; }
static inline int         Gia_ObjCellFanin( Gia_Man_t * p, int iLit, int i ){ return Gia_ObjCellFanins(p, iLit)[i];                                         }
static inline int         Gia_ObjCellId( Gia_Man_t * p, int iLit )          { return Gia_ObjCellFanins(p, iLit)[Gia_ObjCellSize(p, iLit)];                  }

// ITERATORS, PART TWO: the mapping iterators, thirteen macros in three groups matching the three
// representations above. Each group has the same shape - one macro to walk the mapped objects and
// one or more to walk the fanins of one of them - and all of them iterate over IDENTIFIERS, never
// over objects, except Gia_LutForEachFaninObj which materializes the fanin object for the body.
// Of the walkers that range over object identifiers, Gia_ManForEachLut and Gia_ManForEachLut2 start
// at 1 to skip the constant, and Gia_ManForEachLutReverse and Gia_ManForEachLut2Reverse stop at
// i > 0 and so skip it as well. Gia_ManForEachCell is the exception that starts at 2 and runs to
// twice the object count, because the cell map is indexed by literal and literals 0 and 1 are the
// constants.
// Gia_LutForEachFaninIndex exposes the position of the fanin inside p->vMapping as well as its
// identifier, for a caller that means to overwrite it; note that, exactly as with the static-fanout
// index macro above, that assignment sits unguarded in the loop condition and a computed index of 0
// would end the loop - reachable only if the mapping header were malformed, since a record never
// starts at 0. The Lut2Vec pair walks a caller-supplied list of identifiers rather than the whole
// network, and its guard is the entry pointer itself, so a null entry would stop it.
#define Gia_ManForEachLut( p, i )                                       \
    for ( i = 1; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsLut(p, i) ) {} else
#define Gia_ManForEachLutReverse( p, i )                                \
    for ( i = Gia_ManObjNum(p) - 1; i > 0; i-- ) if ( !Gia_ObjIsLut(p, i) ) {} else
#define Gia_LutForEachFanin( p, i, iFan, k )                            \
    for ( k = 0; k < Gia_ObjLutSize(p,i) && ((iFan = Gia_ObjLutFanins(p,i)[k]),1); k++ )
#define Gia_LutForEachFaninIndex( p, i, iFan, k, Index )                            \
    for ( k = 0; k < Gia_ObjLutSize(p,i) && (Index = Vec_IntEntry(p->vMapping, i)+1+k) && ((iFan = Vec_IntEntry(p->vMapping, Index)),1); k++ )
#define Gia_LutForEachFaninObj( p, i, pFanin, k )                       \
    for ( k = 0; k < Gia_ObjLutSize(p,i) && ((pFanin = Gia_ManObj(p, Gia_ObjLutFanins(p,i)[k])),1); k++ )

#define Gia_ManForEachLut2( p, i )                                      \
    for ( i = 1; i < Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsLut2(p, i) ) {} else
#define Gia_ManForEachLut2Reverse( p, i )                               \
    for ( i = Gia_ManObjNum(p) - 1; i > 0; i-- ) if ( !Gia_ObjIsLut2(p, i) ) {} else
#define Gia_ManForEachLut2Vec( vIds, p, vVec, iObj, i )                 \
    for ( i = 0; i < Vec_IntSize(vIds) && (vVec = Vec_WecEntry(p->vMapping2, (iObj = Vec_IntEntry(vIds, i)))); i++ )
#define Gia_ManForEachLut2VecReverse( vIds, p, vVec, iObj, i )          \
    for ( i = Vec_IntSize(vIds)-1; i >= 0 && (vVec = Vec_WecEntry(p->vMapping2, (iObj = Vec_IntEntry(vIds, i)))); i-- )
#define Gia_LutForEachFanin2( p, i, iFan, k )                           \
    for ( k = 0; k < Gia_ObjLutSize2(p,i) && ((iFan = Gia_ObjLutFanin2(p,i,k)),1); k++ )
#define Gia_LutForEachFanout2( p, i, iFan, k )                          \
    for ( k = 0; k < Gia_ObjLutFanoutNum2(p,i) && ((iFan = Gia_ObjLutFanout2(p,i,k)),1); k++ )

#define Gia_ManForEachCell( p, i )                                      \
    for ( i = 2; i < 2*Gia_ManObjNum(p); i++ ) if ( !Gia_ObjIsCell(p, i) ) {} else
#define Gia_CellForEachFanin( p, i, iFanLit, k )                        \
    for ( k = 0; k < Gia_ObjCellSize(p,i) && ((iFanLit = Gia_ObjCellFanins(p,i)[k]),1); k++ )

////////////////////////////////////////////////////////////////////////
///                      MACRO DEFINITIONS                           ///
////////////////////////////////////////////////////////////////////////

// ITERATORS, PART THREE: walking the network. Everything from here to the end of the section is a
// for-loop header, so the caller's next statement or block becomes the body. Three conventions run
// through the whole set and repay knowing before reading any individual macro.
//   THE FILTER IDIOM. A macro that visits a subset ends in "if ( !predicate ) {} else", which makes
// the caller's body the else branch and skips everything else. A consequence: an else written
// immediately after such a loop would bind to that if, so a body that needs a trailing else must be
// braced.
//   THE ASSIGNMENT-AS-GUARD IDIOM. Most of these put the assignment that hands the body its object
// inside the loop condition, so iteration would stop if the assigned value were ever zero. That is
// safe here for a specific reason in each case - Gia_ManObj returns an address inside the array and
// so is never null, a combinational-input or -output identifier is never 0 because object 0 is the
// constant - and not because the pattern is generally sound. Where no such guarantee exists the
// macros wrap the assignment as (expr, 1) instead; compare Gia_ManForEachCoDriverId with
// Gia_ManForEachCoId.
//   OBJECTS VERSUS IDENTIFIERS. Nearly every walker comes in both forms, and the Id form is not just
// a convenience: an identifier stays valid across an append while a Gia_Obj_t * does not, for the
// reason spelled out at Gia_ManAppendObj. A loop that may add objects to the network it is walking
// wants the Id form.
// Individual points that are easy to misread:
//  - Gia_ManForEachObjReverse bounds at i >= 0 and so DOES visit the constant object, whereas
//    Gia_ManForEachObjReverse1, Gia_ManForEachAndReverse and Gia_ManForEachAndReverseId bound at
//    i > 0 and skip it. The forward walkers make the same distinction through their start value.
//  - Gia_ManForEachBuf disables itself: its start is Gia_ManBufNum(p) ? 0 : p->nObjs, so on a
//    manager with no buffers the loop body never runs and nothing is scanned. Gia_ManForEachBufId
//    has no such shortcut and always walks the whole array.
//  - The AND walkers start at 0 rather than 1; the constant object is excluded by the predicate
//    rather than by the bound, since Gia_ObjIsAnd is false for it.
//  - Gia_ManForEachMux and Gia_ManForEachMuxId filter on Gia_ObjIsMuxId and therefore report nothing
//    at all in a manager that carries no pMuxes, rather than reporting an error.
//  - The terminal walkers go through vCis and vCos, so they visit in construction order, and the Pi,
//    Po, Ro and Ri forms are range slices of those same two lists - which is once again why the
//    append order matters. Gia_ManForEachRiRo advances both halves of every register in lockstep.
//  - The ...Vec forms walk a caller-supplied list of identifiers instead of the whole network, and
//    Gia_ManForEachObjVecLit walks a list of LITERALS, handing the body the object and its polarity
//    separately.
#define Gia_ManForEachObj( p, pObj, i )                                 \
    for ( i = 0; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )
#define Gia_ManForEachObj1( p, pObj, i )                                \
    for ( i = 1; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )
#define Gia_ManForEachObjVec( vVec, p, pObj, i )                        \
    for ( i = 0; (i < Vec_IntSize(vVec)) && ((pObj) = Gia_ManObj(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachObjVecStart( vVec, p, pObj, i, Start )            \
    for ( i = Start; (i < Vec_IntSize(vVec)) && ((pObj) = Gia_ManObj(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachObjVecStop( vVec, p, pObj, i, Stop )              \
    for ( i = 0; (i < Stop) && ((pObj) = Gia_ManObj(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachObjVecStartStop( vVec, p, pObj, i, Start, Stop )  \
    for ( i = Start; (i < Stop) && ((pObj) = Gia_ManObj(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachObjVecReverse( vVec, p, pObj, i )                 \
    for ( i = Vec_IntSize(vVec) - 1; (i >= 0) && ((pObj) = Gia_ManObj(p, Vec_IntEntry(vVec,i))); i-- )
#define Gia_ManForEachObjVecLit( vVec, p, pObj, fCompl, i )             \
    for ( i = 0; (i < Vec_IntSize(vVec)) && ((pObj) = Gia_ManObj(p, Abc_Lit2Var(Vec_IntEntry(vVec,i)))) && (((fCompl) = Abc_LitIsCompl(Vec_IntEntry(vVec,i))),1); i++ )
#define Gia_ManForEachObjReverse( p, pObj, i )                          \
    for ( i = p->nObjs - 1; (i >= 0) && ((pObj) = Gia_ManObj(p, i)); i-- )
#define Gia_ManForEachObjReverse1( p, pObj, i )                         \
    for ( i = p->nObjs - 1; (i > 0) && ((pObj) = Gia_ManObj(p, i)); i-- )
#define Gia_ManForEachBuf( p, pObj, i )                                 \
    for ( i = Gia_ManBufNum(p) ? 0 : p->nObjs; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )      if ( !Gia_ObjIsBuf(pObj) ) {} else
#define Gia_ManForEachBufId( p, i )                                     \
    for ( i = 0; (i < p->nObjs); i++ )                                     if ( !Gia_ObjIsBuf(Gia_ManObj(p, i)) ) {} else
#define Gia_ManForEachAnd( p, pObj, i )                                 \
    for ( i = 0; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )      if ( !Gia_ObjIsAnd(pObj) ) {} else
#define Gia_ManForEachAndId( p, i )                                     \
    for ( i = 0; (i < p->nObjs); i++ )                                     if ( !Gia_ObjIsAnd(Gia_ManObj(p, i)) ) {} else
#define Gia_ManForEachMuxId( p, i )                                     \
    for ( i = 0; (i < p->nObjs); i++ )                                     if ( !Gia_ObjIsMuxId(p, i) ) {} else
#define Gia_ManForEachCand( p, pObj, i )                                \
    for ( i = 0; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )      if ( !Gia_ObjIsCand(pObj) ) {} else
#define Gia_ManForEachAndReverse( p, pObj, i )                          \
    for ( i = p->nObjs - 1; (i > 0) && ((pObj) = Gia_ManObj(p, i)); i-- )  if ( !Gia_ObjIsAnd(pObj) ) {} else
#define Gia_ManForEachAndReverseId( p, i )                              \
    for ( i = p->nObjs - 1; (i > 0); i-- )                                 if ( !Gia_ObjIsAnd(Gia_ManObj(p, i)) ) {} else
#define Gia_ManForEachMux( p, pObj, i )                                 \
    for ( i = 0; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )      if ( !Gia_ObjIsMuxId(p, i) ) {} else
#define Gia_ManForEachCi( p, pObj, i )                                  \
    for ( i = 0; (i < Vec_IntSize(p->vCis)) && ((pObj) = Gia_ManCi(p, i)); i++ )
#define Gia_ManForEachCiId( p, Id, i )                                  \
    for ( i = 0; (i < Vec_IntSize(p->vCis)) && ((Id) = Gia_ObjId(p, Gia_ManCi(p, i))); i++ )
#define Gia_ManForEachCiVec( vVec, p, pObj, i )                         \
    for ( i = 0; (i < Vec_IntSize(vVec)) && ((pObj) = Gia_ManCi(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachCiReverse( p, pObj, i )                           \
    for ( i = Vec_IntSize(p->vCis) - 1; (i >= 0) && ((pObj) = Gia_ManCi(p, i)); i-- )
#define Gia_ManForEachCo( p, pObj, i )                                  \
    for ( i = 0; (i < Vec_IntSize(p->vCos)) && ((pObj) = Gia_ManCo(p, i)); i++ )
#define Gia_ManForEachCoVec( vVec, p, pObj, i )                         \
    for ( i = 0; (i < Vec_IntSize(vVec)) && ((pObj) = Gia_ManCo(p, Vec_IntEntry(vVec,i))); i++ )
#define Gia_ManForEachCoId( p, Id, i )                                  \
    for ( i = 0; (i < Vec_IntSize(p->vCos)) && ((Id) = Gia_ObjId(p, Gia_ManCo(p, i))); i++ )
#define Gia_ManForEachCoReverse( p, pObj, i )                           \
    for ( i = Vec_IntSize(p->vCos) - 1; (i >= 0) && ((pObj) = Gia_ManCo(p, i)); i-- )
#define Gia_ManForEachCoDriver( p, pObj, i )                            \
    for ( i = 0; (i < Vec_IntSize(p->vCos)) && ((pObj) = Gia_ObjFanin0(Gia_ManCo(p, i))); i++ )
#define Gia_ManForEachCoDriverId( p, DriverId, i )                      \
    for ( i = 0; (i < Vec_IntSize(p->vCos)) && (((DriverId) = Gia_ObjFaninId0p(p, Gia_ManCo(p, i))), 1); i++ )
#define Gia_ManForEachPi( p, pObj, i )                                  \
    for ( i = 0; (i < Gia_ManPiNum(p)) && ((pObj) = Gia_ManCi(p, i)); i++ )
#define Gia_ManForEachPo( p, pObj, i )                                  \
    for ( i = 0; (i < Gia_ManPoNum(p)) && ((pObj) = Gia_ManCo(p, i)); i++ )
#define Gia_ManForEachRo( p, pObj, i )                                  \
    for ( i = 0; (i < Gia_ManRegNum(p)) && ((pObj) = Gia_ManCi(p, Gia_ManPiNum(p)+i)); i++ )
#define Gia_ManForEachRi( p, pObj, i )                                  \
    for ( i = 0; (i < Gia_ManRegNum(p)) && ((pObj) = Gia_ManCo(p, Gia_ManPoNum(p)+i)); i++ )
#define Gia_ManForEachRiRo( p, pObjRi, pObjRo, i )                      \
    for ( i = 0; (i < Gia_ManRegNum(p)) && ((pObjRi) = Gia_ManCo(p, Gia_ManPoNum(p)+i)) && ((pObjRo) = Gia_ManCi(p, Gia_ManPiNum(p)+i)); i++ )
#define Gia_ManForEachRoToRiVec( vRoIds, p, pObj, i )                   \
    for ( i = 0; (i < Vec_IntSize(vRoIds)) && ((pObj) = Gia_ObjRoToRi(p, Gia_ManObj(p, Vec_IntEntry(vRoIds, i)))); i++ )

// The box-aware walkers. These four are the only consumers of the four boundary indices declared in
// the manager, and they exist so that a pass over a network with hierarchy boxes can visit the logic
// outside the boxes without first constructing a filtered list. Each simply replaces the usual bound
// with one of those indices, so their correctness rests entirely on those fields having been filled;
// on a manager where they are still zero the two object walkers and the combinational-input walker
// visit nothing, while Gia_ManForEachCoWithBoxes visits every combinational output because its upper
// bound is the size of vCos rather than one of the indices.
#define Gia_ManForEachObjWithBoxes( p, pObj, i )                        \
    for ( i = p->iFirstAndObj; (i < p->iFirstPoObj) && ((pObj) = Gia_ManObj(p, i)); i++ )
#define Gia_ManForEachObjReverseWithBoxes( p, pObj, i )                 \
    for ( i = p->iFirstPoObj - 1; (i >= p->iFirstAndObj) && ((pObj) = Gia_ManObj(p, i)); i-- )
#define Gia_ManForEachCiIdWithBoxes( p, Id, i )                         \
    for ( i = 0; (i < p->iFirstNonPiId) && ((Id) = Gia_ObjId(p, Gia_ManCi(p, i))); i++ )
#define Gia_ManForEachCoWithBoxes( p, pObj, i )                         \
    for ( i = p->iFirstPoId; (i < Vec_IntSize(p->vCos)) && ((pObj) = Gia_ManCo(p, i)); i++ )

////////////////////////////////////////////////////////////////////////
///                    FUNCTION DECLARATIONS                         ///
////////////////////////////////////////////////////////////////////////

/*=== giaAiger.c ===========================================================*/
extern int                 Gia_FileSize( char * pFileName );
extern Gia_Man_t *         Gia_AigerReadFromMemory( char * pContents, int nFileSize, int fGiaSimple, int fSkipStrash, int fCheck );
extern Gia_Man_t *         Gia_AigerRead( char * pFileName, int fGiaSimple, int fSkipStrash, int fCheck );
extern void                Gia_AigerWrite( Gia_Man_t * p, char * pFileName, int fWriteSymbols, int fCompact, int fWriteNewLine );
extern void                Gia_AigerWriteS( Gia_Man_t * p, char * pFileName, int fWriteSymbols, int fCompact, int fWriteNewLine, int fSkipComment );
extern void                Gia_DumpAiger( Gia_Man_t * p, char * pFilePrefix, int iFileNum, int nFileNumDigits );
extern Vec_Str_t *         Gia_AigerWriteIntoMemoryStr( Gia_Man_t * p );
extern Vec_Str_t *         Gia_AigerWriteIntoMemoryStrPart( Gia_Man_t * p, Vec_Int_t * vCis, Vec_Int_t * vAnds, Vec_Int_t * vCos, int nRegs );
extern void                Gia_AigerWriteSimple( Gia_Man_t * pInit, char * pFileName );
/*=== giaBalance.c ===========================================================*/
extern Gia_Man_t *         Gia_ManBalance( Gia_Man_t * p, int fSimpleAnd, int fStrict, int fVerbose );
extern Gia_Man_t *         Gia_ManAreaBalance( Gia_Man_t * p, int fSimpleAnd, int nNewNodesMax, int fVerbose, int fVeryVerbose );
extern Gia_Man_t *         Gia_ManAigSyn2( Gia_Man_t * p, int fOldAlgo, int fCoarsen, int fCutMin, int nRelaxRatio, int fDelayMin, int fVerbose, int fVeryVerbose );
extern Gia_Man_t *         Gia_ManAigSyn3( Gia_Man_t * p, int fVerbose, int fVeryVerbose );
extern Gia_Man_t *         Gia_ManAigSyn4( Gia_Man_t * p, int fVerbose, int fVeryVerbose );
/*=== giaBidec.c ===========================================================*/
extern unsigned *          Gia_ManConvertAigToTruth( Gia_Man_t * p, Gia_Obj_t * pRoot, Vec_Int_t * vLeaves, Vec_Int_t * vTruth, Vec_Int_t * vVisited );
extern Gia_Man_t *         Gia_ManPerformBidec( Gia_Man_t * p, int fVerbose );
/*=== giaCex.c ============================================================*/
extern int                 Gia_ManVerifyCex( Gia_Man_t * pAig, Abc_Cex_t * p, int fDualOut );
extern int                 Gia_ManFindFailedPoCex( Gia_Man_t * pAig, Abc_Cex_t * p, int nOutputs );
extern int                 Gia_ManSetFailedPoCex( Gia_Man_t * pAig, Abc_Cex_t * p );
extern void                Gia_ManCounterExampleValueStart( Gia_Man_t * pGia, Abc_Cex_t * pCex );
extern void                Gia_ManCounterExampleValueStop( Gia_Man_t * pGia );
extern int                 Gia_ManCounterExampleValueLookup( Gia_Man_t * pGia, int Id, int iFrame );
extern Abc_Cex_t *         Gia_ManCexExtendToIncludeCurrentStates( Gia_Man_t * p, Abc_Cex_t * pCex );
extern Abc_Cex_t *         Gia_ManCexExtendToIncludeAllObjects( Gia_Man_t * p, Abc_Cex_t * pCex );
/*=== giaCsatOld.c ============================================================*/
extern Vec_Int_t *         Cbs_ManSolveMiter( Gia_Man_t * pGia, int nConfs, Vec_Str_t ** pvStatus, int fVerbose );
/*=== giaCsat.c ============================================================*/
typedef struct Cbs_Man_t_  Cbs_Man_t;
extern Cbs_Man_t *         Cbs_ManAlloc( Gia_Man_t * pGia );
extern void                Cbs_ManStop( Cbs_Man_t * p );
extern int                 Cbs_ManSolve( Cbs_Man_t * p, Gia_Obj_t * pObj );
extern int                 Cbs_ManSolve2( Cbs_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pObj2 );
extern Vec_Int_t *         Cbs_ManSolveMiterNc( Gia_Man_t * pGia, int nConfs, Vec_Str_t ** pvStatus, int f0Proved, int fVerbose );
extern void                Cbs_ManSetConflictNum( Cbs_Man_t * p, int Num );
extern Vec_Int_t *         Cbs_ReadModel( Cbs_Man_t * p );
/*=== giaCTas.c ============================================================*/
extern Vec_Int_t *         Tas_ManSolveMiterNc( Gia_Man_t * pGia, int nConfs, Vec_Str_t ** pvStatus, int fVerbose );
/*=== giaCof.c =============================================================*/
extern void                Gia_ManPrintFanio( Gia_Man_t * pGia, int nNodes );
extern Gia_Man_t *         Gia_ManDupCof( Gia_Man_t * p, int iVar );
extern Gia_Man_t *         Gia_ManDupCofAllInt( Gia_Man_t * p, Vec_Int_t * vSigs, int fVerbose );
extern Gia_Man_t *         Gia_ManDupCofAll( Gia_Man_t * p, int nFanLim, int fVerbose );
/*=== giaDecs.c ============================================================*/
extern int                 Gia_ResubVarNum( Vec_Int_t * vResub );
extern word                Gia_ResubToTruth6( Vec_Int_t * vResub );
extern int                 Gia_ManEvalSolutionOne( Gia_Man_t * p, Vec_Wrd_t * vSims, Vec_Wrd_t * vIsfs, Vec_Int_t * vCands, Vec_Int_t * vSet, int nWords, int fVerbose );
extern Vec_Int_t *         Gia_ManDeriveSolutionOne( Gia_Man_t * p, Vec_Wrd_t * vSims, Vec_Wrd_t * vIsfs, Vec_Int_t * vCands, Vec_Int_t * vSet, int nWords, int Type );
/*=== giaDfs.c ============================================================*/
extern void                Gia_ManCollectCis( Gia_Man_t * p, int * pNodes, int nNodes, Vec_Int_t * vSupp );
extern void                Gia_ManCollectAnds_rec( Gia_Man_t * p, int iObj, Vec_Int_t * vNodes );
extern void                Gia_ManCollectAnds( Gia_Man_t * p, int * pNodes, int nNodes, Vec_Int_t * vNodes, Vec_Int_t * vLeaves );
extern Vec_Int_t *         Gia_ManCollectAndsAll( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManCollectNodesCis( Gia_Man_t * p, int * pNodes, int nNodes );
extern int                 Gia_ManSuppSize( Gia_Man_t * p, int * pNodes, int nNodes );
extern int                 Gia_ManConeSize( Gia_Man_t * p, int * pNodes, int nNodes );
extern Vec_Vec_t *         Gia_ManLevelize( Gia_Man_t * p );
extern Vec_Wec_t *         Gia_ManLevelizeR( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManOrderReverse( Gia_Man_t * p );
extern void                Gia_ManCollectTfi( Gia_Man_t * p, Vec_Int_t * vRoots, Vec_Int_t * vNodes );
extern void                Gia_ManCollectTfo( Gia_Man_t * p, Vec_Int_t * vRoots, Vec_Int_t * vNodes );
/*=== giaDup.c ============================================================*/
extern void                Gia_ManDupRemapLiterals( Vec_Int_t * vLits, Gia_Man_t * p );
extern void                Gia_ManDupRemapEquiv( Gia_Man_t * pNew, Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupOrderDfs( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupOrderDfsChoices( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupOrderDfsReverse( Gia_Man_t * p, int fRevFans, int fRevOuts );
extern Gia_Man_t *         Gia_ManDupOutputGroup( Gia_Man_t * p, int iOutStart, int iOutStop );
extern Gia_Man_t *         Gia_ManDupOutputVec( Gia_Man_t * p, Vec_Int_t * vOutPres );
extern Gia_Man_t *         Gia_ManDupSelectedOutputs( Gia_Man_t * p, Vec_Int_t * vOutsLeft );
extern Gia_Man_t *         Gia_ManDupOrderAiger( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupLastPis( Gia_Man_t * p, int nLastPis );
extern Gia_Man_t *         Gia_ManDupFlip( Gia_Man_t * p, int * pInitState );
extern Gia_Man_t *         Gia_ManDupCycled( Gia_Man_t * pAig, Abc_Cex_t * pCex, int nFrames );
extern Gia_Man_t *         Gia_ManDup( Gia_Man_t * p );  
extern Gia_Man_t *         Gia_ManDupNoBuf( Gia_Man_t * p );  
extern Gia_Man_t *         Gia_ManDupMap( Gia_Man_t * p, Vec_Int_t * vMap );
extern Gia_Man_t *         Gia_ManDup2( Gia_Man_t * p1, Gia_Man_t * p2 );
extern Gia_Man_t *         Gia_ManDupWithAttributes( Gia_Man_t * p );  
extern Gia_Man_t *         Gia_ManDupRemovePis( Gia_Man_t * p, int nRemPis );
extern Gia_Man_t *         Gia_ManDupZero( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupPerm( Gia_Man_t * p, Vec_Int_t * vPiPerm );
extern Gia_Man_t *         Gia_ManDupPermFlop( Gia_Man_t * p, Vec_Int_t * vFfPerm );
extern Gia_Man_t *         Gia_ManDupPermFlopGap( Gia_Man_t * p, Vec_Int_t * vFfPerm );
extern void                Gia_ManDupAppend( Gia_Man_t * p, Gia_Man_t * pTwo, int fShareCis );
extern void                Gia_ManDupAppendShare( Gia_Man_t * p, Gia_Man_t * pTwo );
extern Gia_Man_t *         Gia_ManDupAppendNew( Gia_Man_t * pOne, Gia_Man_t * pTwo );
extern Gia_Man_t *         Gia_ManDupAppendCones( Gia_Man_t * p, Gia_Man_t ** ppCones, int nCones, int fOnlyRegs );
extern Gia_Man_t *         Gia_ManDupSelf( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupFlopClass( Gia_Man_t * p, int iClass );
extern Gia_Man_t *         Gia_ManDupMarked( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupTimes( Gia_Man_t * p, int nTimes );  
extern Gia_Man_t *         Gia_ManDupDfs( Gia_Man_t * p );  
extern Gia_Man_t *         Gia_ManDupDfsOnePo( Gia_Man_t * p, int iPo );
extern Gia_Man_t *         Gia_ManDupDfsRehash( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupCofactorVar( Gia_Man_t * p, int iVar, int Value );  
extern Gia_Man_t *         Gia_ManDupCofactorObj( Gia_Man_t * p, int iObj, int Value );  
extern Gia_Man_t *         Gia_ManDupMux( int iVar, Gia_Man_t * pCof1, Gia_Man_t * pCof0 );
extern Gia_Man_t *         Gia_ManDupBlock( Gia_Man_t * p, int nBlock );
extern Gia_Man_t *         Gia_ManDupExist( Gia_Man_t * p, int iVar );
extern Gia_Man_t *         Gia_ManDupUniv( Gia_Man_t * p, int iVar );
extern Gia_Man_t *         Gia_ManDupDfsSkip( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupDfsCone( Gia_Man_t * p, Gia_Obj_t * pObj );
extern Gia_Man_t *         Gia_ManDupConeSupp( Gia_Man_t * p, int iLit, Vec_Int_t * vCiIds );
extern int                 Gia_ManDupConeBack( Gia_Man_t * p, Gia_Man_t * pNew, Vec_Int_t * vCiIds );
extern int                 Gia_ManDupConeBackObjs( Gia_Man_t * p, Gia_Man_t * pNew, Vec_Int_t * vObjs );
extern Gia_Man_t *         Gia_ManDupDfsNode( Gia_Man_t * p, Gia_Obj_t * pObj );
extern Gia_Man_t *         Gia_ManDupDfsLitArray( Gia_Man_t * p, Vec_Int_t * vLits );
extern Gia_Man_t *         Gia_ManDupTrimmed( Gia_Man_t * p, int fTrimCis, int fTrimCos, int fDualOut, int OutValue );
extern Gia_Man_t *         Gia_ManDupOntop( Gia_Man_t * p, Gia_Man_t * p2 );
extern Gia_Man_t *         Gia_ManDupWithNewPo( Gia_Man_t * p1, Gia_Man_t * p2 );
extern Gia_Man_t *         Gia_ManDupDfsCiMap( Gia_Man_t * p, int * pCi2Lit, Vec_Int_t * vLits );
extern Gia_Man_t *         Gia_ManPermuteInputs( Gia_Man_t * p, int nPpis, int nExtra );
extern Gia_Man_t *         Gia_ManDupDfsClasses( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupTopAnd( Gia_Man_t * p, int fVerbose );
extern Gia_Man_t *         Gia_ManMiter( Gia_Man_t * pAig0, Gia_Man_t * pAig1, int nInsDup, int fDualOut, int fSeq, int fImplic, int fVerbose );
extern Gia_Man_t *         Gia_ManMiterInverse( Gia_Man_t * pBot, Gia_Man_t * pTop, int fDualOut, int fVerbose );
extern Gia_Man_t *         Gia_ManDupAndOr( Gia_Man_t * p, int nOuts, int fUseOr, int fCompl );
extern Gia_Man_t *         Gia_ManDupZeroUndc( Gia_Man_t * p, char * pInit, int nNewPis, int fGiaSimple, int fVerbose );
extern Gia_Man_t *         Gia_ManMiter2( Gia_Man_t * p, char * pInit, int fVerbose );
extern Gia_Man_t *         Gia_ManTransformMiter( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManTransformMiter2( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManTransformToDual( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManTransformTwoWord2DualOutput( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManChoiceMiter( Vec_Ptr_t * vGias );
extern Gia_Man_t *         Gia_ManDupWithConstraints( Gia_Man_t * p, Vec_Int_t * vPoTypes );
extern Gia_Man_t *         Gia_ManDupCones( Gia_Man_t * p, int * pPos, int nPos, int fTrimPis );
extern Gia_Man_t *         Gia_ManDupAndCones( Gia_Man_t * p, int * pAnds, int nAnds, int fTrimPis );
extern Gia_Man_t *         Gia_ManDupAndConesLimit( Gia_Man_t * p, int * pAnds, int nAnds, int Level );
extern Gia_Man_t *         Gia_ManDupAndConesLimit2( Gia_Man_t * p, int * pAnds, int nAnds, int Level );
extern Gia_Man_t *         Gia_ManDupOneHot( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupLevelized( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupFromVecs( Gia_Man_t * p, Vec_Int_t * vCis, Vec_Int_t * vAnds, Vec_Int_t * vCos, int nRegs );
extern Gia_Man_t *         Gia_ManDupSliced( Gia_Man_t * p, int nSuppMax );
extern Gia_Man_t *         Gia_ManDupDemiter( Gia_Man_t * p, int fVerbose );
extern Gia_Man_t *         Gia_ManDemiterToDual( Gia_Man_t * p );
extern int                 Gia_ManDemiterDual( Gia_Man_t * p, Gia_Man_t ** pp0, Gia_Man_t ** pp1 );
extern int                 Gia_ManDemiterTwoWords( Gia_Man_t * p, Gia_Man_t ** pp0, Gia_Man_t ** pp1 );
extern void                Gia_ManProdAdderGen( int nArgA, int nArgB, int Seed, int fSigned, int fCla );
typedef struct Gia_ChMan_t_ Gia_ChMan_t;
extern Gia_ChMan_t *       Gia_ManDupChoicesStart( Gia_Man_t * pGia );
extern void                Gia_ManDupChoicesAdd( Gia_ChMan_t * pMan, Gia_Man_t * pGia );
extern Gia_Man_t *         Gia_ManDupChoicesFinish( Gia_ChMan_t * pMan );
extern Vec_Int_t *         Gia_ManComputeMffc( Gia_Man_t * p, Vec_Int_t * vLits, Vec_Int_t * vOuts );
extern Gia_Man_t *         Gia_ManDupExtractMffc( Gia_Man_t * p, Vec_Int_t * vLits, Vec_Int_t * vAnds, Vec_Int_t * vCos );
/*=== giaEdge.c ==========================================================*/
extern void                Gia_ManEdgeFromArray( Gia_Man_t * p, Vec_Int_t * vArray );
extern Vec_Int_t *         Gia_ManEdgeToArray( Gia_Man_t * p );
extern void                Gia_ManConvertPackingToEdges( Gia_Man_t * p );
extern int                 Gia_ObjCheckEdge( Gia_Man_t * p, int iObj, int iNext );
extern int                 Gia_ManEvalEdgeDelay( Gia_Man_t * p );
extern int                 Gia_ManEvalEdgeCount( Gia_Man_t * p );
extern int                 Gia_ManComputeEdgeDelay( Gia_Man_t * p, int fUseTwo );
extern int                 Gia_ManComputeEdgeDelay2( Gia_Man_t * p );
extern void                Gia_ManUpdateMapping( Gia_Man_t * p, Vec_Int_t * vNodes, Vec_Wec_t * vWin );
extern int                 Gia_ManEvalWindow( Gia_Man_t * p, Vec_Int_t * vLeaves, Vec_Int_t * vNodes, Vec_Wec_t * vWin, Vec_Int_t * vTemp, int fUseTwo );
/*=== giaEnable.c ==========================================================*/
extern void                Gia_ManDetectSeqSignals( Gia_Man_t * p, int fSetReset, int fVerbose );
extern Gia_Man_t *         Gia_ManUnrollAndCofactor( Gia_Man_t * p, int nFrames, int nFanMax, int fVerbose );
extern Gia_Man_t *         Gia_ManRemoveEnables( Gia_Man_t * p );
/*=== giaEquiv.c ==========================================================*/
extern void                Gia_ManOrigIdsInit( Gia_Man_t * p );
extern void                Gia_ManOrigIdsStart( Gia_Man_t * p );
extern void                Gia_ManOrigIdsRemap( Gia_Man_t * p, Gia_Man_t * pNew );
extern Gia_Man_t *         Gia_ManOrigIdsReduce( Gia_Man_t * p, Vec_Int_t * vPairs );
extern Gia_Man_t *         Gia_ManComputeGiaEquivs( Gia_Man_t * pGia, int nConfs, int fVerbose );
extern void                Gia_ManEquivFixOutputPairs( Gia_Man_t * p );
extern int                 Gia_ManCheckTopoOrder( Gia_Man_t * p );
extern int *               Gia_ManDeriveNexts( Gia_Man_t * p );
extern void                Gia_ManDeriveReprs( Gia_Man_t * p );
extern void                Gia_ManDeriveReprsFromSibls( Gia_Man_t *p );
extern int                 Gia_ManEquivCountLits( Gia_Man_t * p );
extern int                 Gia_ManEquivCountLitsAll( Gia_Man_t * p );
extern int                 Gia_ManEquivCountClasses( Gia_Man_t * p );
extern void                Gia_ManEquivPrintOne( Gia_Man_t * p, int i, int Counter );
extern void                Gia_ManEquivPrintClasses( Gia_Man_t * p, int fVerbose, float Mem );
extern Gia_Man_t *         Gia_ManEquivReduce( Gia_Man_t * p, int fUseAll, int fDualOut, int fSkipPhase, int fVerbose );
extern Gia_Man_t *         Gia_ManEquivReduceAndRemap( Gia_Man_t * p, int fSeq, int fMiterPairs );
extern int                 Gia_ManEquivSetColors( Gia_Man_t * p, int fVerbose );
extern Gia_Man_t *         Gia_ManSpecReduce( Gia_Man_t * p, int fDualOut, int fSynthesis, int fReduce, int fSkipSome, int fVerbose );
extern Gia_Man_t *         Gia_ManSpecReduceInit( Gia_Man_t * p, Abc_Cex_t * pInit, int nFrames, int fDualOut );
extern Gia_Man_t *         Gia_ManSpecReduceInitFrames( Gia_Man_t * p, Abc_Cex_t * pInit, int nFramesMax, int * pnFrames, int fDualOut, int nMinOutputs );
extern void                Gia_ManEquivTransform( Gia_Man_t * p, int fVerbose );
extern void                Gia_ManEquivImprove( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManEquivToChoices( Gia_Man_t * p, int nSnapshots );
extern int                 Gia_ManCountChoiceNodes( Gia_Man_t * p );
extern int                 Gia_ManCountChoices( Gia_Man_t * p );
extern int                 Gia_ManFilterEquivsForSpeculation( Gia_Man_t * pGia, char * pName1, char * pName2, int fLatchA, int fLatchB );
extern int                 Gia_ManFilterEquivsUsingParts( Gia_Man_t * pGia, char * pName1, char * pName2 );
extern void                Gia_ManFilterEquivsUsingLatches( Gia_Man_t * pGia, int fFlopsOnly, int fFlopsWith, int fUseRiDrivers );
/*=== giaExist.c =========================================================*/
extern void                Gia_ManQuantSetSuppStart( Gia_Man_t * p );
extern void                Gia_ManQuantSetSuppZero( Gia_Man_t * p );
extern void                Gia_ManQuantSetSuppCi( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ManQuantUpdateCiSupp( Gia_Man_t * p, int iObj );
extern int                 Gia_ManQuantExist( Gia_Man_t * p, int iLit, int(*pFuncCiToKeep)(void *, int), void * pData );
/*=== giaFanout.c =========================================================*/
extern void                Gia_ObjRemoveFanout( Gia_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pFanout );
extern void                Gia_ManFanoutStart( Gia_Man_t * p );
extern void                Gia_ManFanoutStop( Gia_Man_t * p );
extern void                Gia_ManStaticFanoutStart( Gia_Man_t * p );
extern void                Gia_ManStaticFanoutStop( Gia_Man_t * p );
extern void                Gia_ManStaticMappingFanoutStart( Gia_Man_t * p, Vec_Int_t ** pvIndex );
/*=== giaForce.c =========================================================*/
extern void                For_ManExperiment( Gia_Man_t * pGia, int nIters, int fClustered, int fVerbose );
/*=== giaFrames.c =========================================================*/
extern Gia_Man_t *         Gia_ManUnrollDup( Gia_Man_t * p, Vec_Int_t * vLimit );
extern Vec_Ptr_t *         Gia_ManUnrollAbs( Gia_Man_t * p, int nFrames );
extern void *              Gia_ManUnrollStart( Gia_Man_t * pAig, Gia_ParFra_t * pPars );
extern void *              Gia_ManUnrollAdd( void * pMan, int fMax );
extern void                Gia_ManUnrollStop( void * pMan );
extern int                 Gia_ManUnrollLastLit( void * pMan );
extern void                Gia_ManFraSetDefaultParams( Gia_ParFra_t * p );
extern Gia_Man_t *         Gia_ManFrames( Gia_Man_t * pAig, Gia_ParFra_t * pPars );  
extern Gia_Man_t *         Gia_ManFramesInitSpecial( Gia_Man_t * pAig, int nFrames, int fVerbose );
/*=== giaFront.c ==========================================================*/
extern Gia_Man_t *         Gia_ManFront( Gia_Man_t * p );
extern void                Gia_ManFrontTest( Gia_Man_t * p );
/*=== giaFx.c ==========================================================*/
extern Gia_Man_t *         Gia_ManPerformFx( Gia_Man_t * p, int nNewNodesMax, int LitCountMax, int fReverse, int fVerbose, int fVeryVerbose );
/*=== giaHash.c ===========================================================*/
extern void                Gia_ManHashAlloc( Gia_Man_t * p ); 
extern void                Gia_ManHashStart( Gia_Man_t * p ); 
extern void                Gia_ManHashStop( Gia_Man_t * p );
extern int                 Gia_ManHashXorReal( Gia_Man_t * p, int iLit0, int iLit1 );
extern int                 Gia_ManHashMuxReal( Gia_Man_t * p, int iLitC, int iLit1, int iLit0 );
extern int                 Gia_ManHashAnd( Gia_Man_t * p, int iLit0, int iLit1 ); 
extern int                 Gia_ManHashOr( Gia_Man_t * p, int iLit0, int iLit1 ); 
extern int                 Gia_ManHashXor( Gia_Man_t * p, int iLit0, int iLit1 ); 
extern int                 Gia_ManHashMux( Gia_Man_t * p, int iCtrl, int iData1, int iData0 );
extern int                 Gia_ManHashMaj( Gia_Man_t * p, int iData0, int iData1, int iData2 );
extern int                 Gia_ManHashAndTry( Gia_Man_t * p, int iLit0, int iLit1 );
extern Gia_Man_t *         Gia_ManRehash( Gia_Man_t * p, int fAddStrash );
extern void                Gia_ManHashProfile( Gia_Man_t * p );
extern int                 Gia_ManHashLookupInt( Gia_Man_t * p, int iLit0, int iLit1 );
extern int                 Gia_ManHashLookup( Gia_Man_t * p, Gia_Obj_t * p0, Gia_Obj_t * p1 );
extern int                 Gia_ManHashAndMulti( Gia_Man_t * p, Vec_Int_t * vLits );
extern int                 Gia_ManHashAndMulti2( Gia_Man_t * p, Vec_Int_t * vLits );
extern int                 Gia_ManHashDualMiter( Gia_Man_t * p, Vec_Int_t * vOuts );
/*=== giaIf.c ===========================================================*/
extern void                Gia_ManPrintOutputLutStats( Gia_Man_t * p );
extern void                Gia_ManPrintMappingStats( Gia_Man_t * p, char * pDumpFile );
extern void                Gia_ManPrintPackingStats( Gia_Man_t * p );
extern void                Gia_ManPrintLutStats( Gia_Man_t * p );
extern int                 Gia_ManLutFaninCount( Gia_Man_t * p );
extern int                 Gia_ManLutSizeMax( Gia_Man_t * p );
extern int                 Gia_ManLutNum( Gia_Man_t * p );
extern int                 Gia_ManLutLevel( Gia_Man_t * p, int ** ppLevels );
extern void                Gia_ManLutParams( Gia_Man_t * p, int * pnCurLuts, int * pnCurEdges, int * pnCurLevels );
extern void                Gia_ManSetRefsMapped( Gia_Man_t * p );
extern void                Gia_ManSetLutRefs( Gia_Man_t * p );  
extern void                Gia_ManSetIfParsDefault( void * pIfPars );
extern void                Gia_ManMappingVerify( Gia_Man_t * p );
extern void                Gia_ManTransferMapping( Gia_Man_t * p, Gia_Man_t * pGia );
extern void                Gia_ManTransferPacking( Gia_Man_t * p, Gia_Man_t * pGia );
extern void                Gia_ManTransferTiming( Gia_Man_t * p, Gia_Man_t * pGia );
extern Gia_Man_t *         Gia_ManPerformMapping( Gia_Man_t * p, void * pIfPars );
extern Gia_Man_t *         Gia_ManPerformSopBalance( Gia_Man_t * p, int nCutNum, int nRelaxRatio, int fVerbose );
extern Gia_Man_t *         Gia_ManPerformDsdBalance( Gia_Man_t * p, int nLutSize, int nCutNum, int nRelaxRatio, int fVerbose );
extern Gia_Man_t *         Gia_ManDupHashMapping( Gia_Man_t * p );
/*=== giaJf.c ===========================================================*/
extern void                Jf_ManSetDefaultPars( Jf_Par_t * pPars );
extern Gia_Man_t *         Jf_ManPerformMapping( Gia_Man_t * pGia, Jf_Par_t * pPars );
extern Gia_Man_t *         Jf_ManDeriveCnf( Gia_Man_t * p, int fCnfObjIds );
/*=== giaIso.c ===========================================================*/
extern Gia_Man_t *         Gia_ManIsoCanonicize( Gia_Man_t * p, int fVerbose );
extern Gia_Man_t *         Gia_ManIsoReduce( Gia_Man_t * p, Vec_Ptr_t ** pvPosEquivs, Vec_Ptr_t ** pvPiPerms, int fEstimate, int fDualOut, int fVerbose, int fVeryVerbose );
extern Gia_Man_t *         Gia_ManIsoReduce2( Gia_Man_t * p, Vec_Ptr_t ** pvPosEquivs, Vec_Ptr_t ** pvPiPerms, int fEstimate, int fBetterQual, int fDualOut, int fVerbose, int fVeryVerbose );
/*=== giaLf.c ===========================================================*/
extern void                Lf_ManSetDefaultPars( Jf_Par_t * pPars );
extern Gia_Man_t *         Lf_ManPerformMapping( Gia_Man_t * pGia, Jf_Par_t * pPars );
extern Gia_Man_t *         Gia_ManPerformLfMapping( Gia_Man_t * p, Jf_Par_t * pPars, int fNormalized );
/*=== giaLogic.c ===========================================================*/
extern void                Gia_ManTestDistance( Gia_Man_t * p );
extern void                Gia_ManSolveProblem( Gia_Man_t * pGia, Emb_Par_t * pPars );
 /*=== giaMan.c ===========================================================*/
extern Gia_Man_t *         Gia_ManStart( int nObjsMax ); 
extern void                Gia_ManStop( Gia_Man_t * p );  
extern void                Gia_ManStopP( Gia_Man_t ** p );  
extern double              Gia_ManMemory( Gia_Man_t * p );
extern void                Gia_ManPrintStats( Gia_Man_t * p, Gps_Par_t * pPars ); 
extern void                Gia_ManPrintStatsShort( Gia_Man_t * p ); 
extern void                Gia_ManPrintMiterStatus( Gia_Man_t * p ); 
extern void                Gia_ManPrintStatsMiter( Gia_Man_t * p, int fVerbose );
extern void                Gia_ManSetRegNum( Gia_Man_t * p, int nRegs );
extern void                Gia_ManReportImprovement( Gia_Man_t * p, Gia_Man_t * pNew );
extern void                Gia_ManPrintNpnClasses( Gia_Man_t * p );
extern void                Gia_ManDumpVerilog( Gia_Man_t * p, char * pFileName, Vec_Int_t * vObjs, int fVerBufs, int fInter, int fInterComb, int fAssign, int fReverse );
extern void                Gia_ManDumpVerilogNand( Gia_Man_t * p, char * pFileName );
/*=== giaMem.c ===========================================================*/
extern Gia_MmFixed_t *     Gia_MmFixedStart( int nEntrySize, int nEntriesMax );
extern void                Gia_MmFixedStop( Gia_MmFixed_t * p, int fVerbose );
extern char *              Gia_MmFixedEntryFetch( Gia_MmFixed_t * p );
extern void                Gia_MmFixedEntryRecycle( Gia_MmFixed_t * p, char * pEntry );
extern void                Gia_MmFixedRestart( Gia_MmFixed_t * p );
extern int                 Gia_MmFixedReadMemUsage( Gia_MmFixed_t * p );
extern int                 Gia_MmFixedReadMaxEntriesUsed( Gia_MmFixed_t * p );
extern Gia_MmFlex_t *      Gia_MmFlexStart();
extern void                Gia_MmFlexStop( Gia_MmFlex_t * p, int fVerbose );
extern char *              Gia_MmFlexEntryFetch( Gia_MmFlex_t * p, int nBytes );
extern void                Gia_MmFlexRestart( Gia_MmFlex_t * p );
extern int                 Gia_MmFlexReadMemUsage( Gia_MmFlex_t * p );
extern Gia_MmStep_t *      Gia_MmStepStart( int nSteps );
extern void                Gia_MmStepStop( Gia_MmStep_t * p, int fVerbose );
extern char *              Gia_MmStepEntryFetch( Gia_MmStep_t * p, int nBytes );
extern void                Gia_MmStepEntryRecycle( Gia_MmStep_t * p, char * pEntry, int nBytes );
extern int                 Gia_MmStepReadMemUsage( Gia_MmStep_t * p );
/*=== giaMf.c ===========================================================*/
extern void                Mf_ManSetDefaultPars( Jf_Par_t * pPars );
extern Gia_Man_t *         Mf_ManPerformMapping( Gia_Man_t * pGia, Jf_Par_t * pPars );
extern void *              Mf_ManGenerateCnf( Gia_Man_t * pGia, int nLutSize, int fCnfObjIds, int fAddOrCla, int fMapping, int fVerbose );
/*=== giaMini.c ===========================================================*/
extern Gia_Man_t *         Gia_ManReadMiniAig( char * pFileName, int fGiaSimple );
extern void                Gia_ManWriteMiniAig( Gia_Man_t * pGia, char * pFileName );
extern Gia_Man_t *         Gia_ManReadMiniLut( char * pFileName );
extern void                Gia_ManWriteMiniLut( Gia_Man_t * pGia, char * pFileName );
/*=== giaMinLut.c ===========================================================*/
extern word *              Gia_ManCountFraction( Gia_Man_t * p, Vec_Wrd_t * vSimI, Vec_Int_t * vSupp, int Thresh, int fVerbose, int * pCare );
extern Vec_Int_t *         Gia_ManCollectSuppNew( Gia_Man_t * p, int iOut, int nOuts );
/*=== giaMuxes.c ===========================================================*/
extern void                Gia_ManCountMuxXor( Gia_Man_t * p, int * pnMuxes, int * pnXors );
extern void                Gia_ManPrintMuxStats( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupMuxes( Gia_Man_t * p, int Limit );
extern Gia_Man_t *         Gia_ManDupNoMuxes( Gia_Man_t * p, int fSkipBufs );
/*=== giaPat.c ===========================================================*/
extern void                Gia_SatVerifyPattern( Gia_Man_t * p, Gia_Obj_t * pRoot, Vec_Int_t * vCex, Vec_Int_t * vVisit );
/*=== giaRetime.c ===========================================================*/
extern Gia_Man_t *         Gia_ManRetimeForward( Gia_Man_t * p, int nMaxIters, int fVerbose );
/*=== giaSat.c ============================================================*/
extern int                 Sat_ManTest( Gia_Man_t * pGia, Gia_Obj_t * pObj, int nConfsMax );
/*=== giaScl.c ============================================================*/
extern int                 Gia_ManSeqMarkUsed( Gia_Man_t * p );
extern int                 Gia_ManCombMarkUsed( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManCleanup( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManCleanupOutputs( Gia_Man_t * p, int nOutputs );
extern Gia_Man_t *         Gia_ManSeqCleanup( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManSeqStructSweep( Gia_Man_t * p, int fConst, int fEquiv, int fVerbose );
/*=== giaShow.c ===========================================================*/
extern void                Gia_ManShow( Gia_Man_t * pMan, Vec_Int_t * vBold, int fAdders, int fFadds, int fPath );
/*=== giaShrink.c ===========================================================*/
extern Gia_Man_t *         Gia_ManMapShrink4( Gia_Man_t * p, int fKeepLevel, int fVerbose );
extern Gia_Man_t *         Gia_ManMapShrink6( Gia_Man_t * p, int nFanoutMax, int fKeepLevel, int fVerbose );
/*=== giaSopb.c ============================================================*/
extern Gia_Man_t *         Gia_ManExtractWindow( Gia_Man_t * p, int LevelMax, int nTimeWindow, int fVerbose );
extern Gia_Man_t *         Gia_ManPerformSopBalanceWin( Gia_Man_t * p, int LevelMax, int nTimeWindow, int nCutNum, int nRelaxRatio, int fVerbose );
extern Gia_Man_t *         Gia_ManPerformDsdBalanceWin( Gia_Man_t * p, int LevelMax, int nTimeWindow, int nLutSize, int nCutNum, int nRelaxRatio, int fVerbose );
/*=== giaSort.c ============================================================*/
extern int *               Gia_SortFloats( float * pArray, int * pPerm, int nSize );
/*=== giaSim.c ============================================================*/
extern void                Gia_ManSimSetDefaultParams( Gia_ParSim_t * p );
extern int                 Gia_ManSimSimulate( Gia_Man_t * pAig, Gia_ParSim_t * pPars );
extern unsigned *          Gia_SimDataExt( Gia_ManSim_t * p, int i );
extern unsigned *          Gia_SimDataCiExt( Gia_ManSim_t * p, int i );
extern unsigned *          Gia_SimDataCoExt( Gia_ManSim_t * p, int i );
extern void                Gia_ManSimInfoInit( Gia_ManSim_t * p );
extern void                Gia_ManSimInfoTransfer( Gia_ManSim_t * p );
extern void                Gia_ManSimulateRound( Gia_ManSim_t * p );
extern void                Gia_ManBuiltInSimStart( Gia_Man_t * p, int nWords, int nObjs );
extern int                 Gia_ManBuiltInSimCheckOver( Gia_Man_t * p, int iLit0, int iLit1 );
extern int                 Gia_ManBuiltInSimCheckEqual( Gia_Man_t * p, int iLit0, int iLit1 );
extern void                Gia_ManBuiltInSimResimulateCone( Gia_Man_t * p, int iLit0, int iLit1 );
extern void                Gia_ManBuiltInSimResimulate( Gia_Man_t * p );
extern int                 Gia_ManBuiltInSimAddPat( Gia_Man_t * p, Vec_Int_t * vPat );
extern void                Gia_ManIncrSimStart( Gia_Man_t * p, int nWords, int nObjs );
extern void                Gia_ManIncrSimSet( Gia_Man_t * p, Vec_Int_t * vObjLits );
extern int                 Gia_ManIncrSimCheckOver( Gia_Man_t * p, int iLit0, int iLit1 );
extern int                 Gia_ManIncrSimCheckEqual( Gia_Man_t * p, int iLit0, int iLit1 );
/*=== giaSimBase.c ============================================================*/
extern Vec_Wrd_t *         Gia_ManSimPatSim( Gia_Man_t * p );
extern Vec_Wrd_t *         Gia_ManSimPatSimOut( Gia_Man_t * pGia, Vec_Wrd_t * vSimsPi, int fOuts );
extern void                Gia_ManSim2ArrayOne( Vec_Wrd_t * vSimsPi, Vec_Int_t * vRes );
extern Vec_Wec_t *         Gia_ManSim2Array( Vec_Ptr_t * vSims );
extern Vec_Wrd_t *         Gia_ManArray2SimOne( Vec_Int_t * vRes );
extern Vec_Ptr_t *         Gia_ManArray2Sim( Vec_Wec_t * vRes );
extern void                Gia_ManPtrWrdDumpBin( char * pFileName, Vec_Ptr_t * p, int fVerbose );
extern Vec_Ptr_t *         Gia_ManPtrWrdReadBin( char * pFileName, int fVerbose );
extern Vec_Str_t *         Gia_ManComputeRange( Gia_Man_t * p );
/*=== giaSpeedup.c ============================================================*/
extern float               Gia_ManDelayTraceLut( Gia_Man_t * p );
extern float               Gia_ManDelayTraceLutPrint( Gia_Man_t * p, int fVerbose );
extern Gia_Man_t *         Gia_ManSpeedup( Gia_Man_t * p, int Percentage, int Degree, int fVerbose, int fVeryVerbose );
/*=== giaSplit.c ============================================================*/
extern void                Gia_ManComputeOneWinStart( Gia_Man_t * p, int nAnds, int fReverse );
extern int                 Gia_ManComputeOneWin( Gia_Man_t * p, int iPivot, Vec_Int_t ** pvRoots, Vec_Int_t ** pvNodes, Vec_Int_t ** pvLeaves, Vec_Int_t ** pvAnds );
/*=== giaStg.c ============================================================*/
extern void                Gia_ManStgPrint( FILE * pFile, Vec_Int_t * vLines, int nIns, int nOuts, int nStates );
extern Gia_Man_t *         Gia_ManStgRead( char * pFileName, int kHot, int fVerbose );
/*=== giaSupp.c ============================================================*/
typedef struct Gia_ManMin_t_ Gia_ManMin_t;
extern Gia_ManMin_t *      Gia_ManSuppStart( Gia_Man_t * pGia );
extern void                Gia_ManSuppStop( Gia_ManMin_t * p );
extern int                 Gia_ManSupportAnd( Gia_ManMin_t * p, int iLit0, int iLit1 );
typedef struct Gia_Man2Min_t_ Gia_Man2Min_t;
extern Gia_Man2Min_t *     Gia_Man2SuppStart( Gia_Man_t * pGia );
extern void                Gia_Man2SuppStop( Gia_Man2Min_t * p );
extern int                 Gia_Man2SupportAnd( Gia_Man2Min_t * p, int iLit0, int iLit1 );
/*=== giaSweep.c ============================================================*/
extern Gia_Man_t *         Gia_ManFraigSweepSimple( Gia_Man_t * p, void * pPars );
extern Gia_Man_t *         Gia_ManSweepWithBoxes( Gia_Man_t * p, void * pParsC, void * pParsS, int fConst, int fEquiv, int fVerbose, int fVerbEquivs );
extern void                Gia_ManCheckIntegrityWithBoxes( Gia_Man_t * p );
/*=== giaSweeper.c ============================================================*/
extern Gia_Man_t *         Gia_SweeperStart( Gia_Man_t * p );
extern void                Gia_SweeperStop( Gia_Man_t * p );
extern int                 Gia_SweeperIsRunning( Gia_Man_t * p );
extern void                Gia_SweeperPrintStats( Gia_Man_t * p );
extern void                Gia_SweeperSetConflictLimit( Gia_Man_t * p, int nConfMax );
extern void                Gia_SweeperSetRuntimeLimit( Gia_Man_t * p, int nSeconds );
extern Vec_Int_t *         Gia_SweeperGetCex( Gia_Man_t * p );
extern int                 Gia_SweeperProbeCreate( Gia_Man_t * p, int iLit );
extern int                 Gia_SweeperProbeDelete( Gia_Man_t * p, int ProbeId );
extern int                 Gia_SweeperProbeUpdate( Gia_Man_t * p, int ProbeId, int iLitNew );
extern int                 Gia_SweeperProbeLit( Gia_Man_t * p, int ProbeId );
extern Vec_Int_t *         Gia_SweeperCollectValidProbeIds( Gia_Man_t * p );
extern int                 Gia_SweeperCondPop( Gia_Man_t * p );
extern void                Gia_SweeperCondPush( Gia_Man_t * p, int ProbeId );
extern Vec_Int_t *         Gia_SweeperCondVector( Gia_Man_t * p );
extern int                 Gia_SweeperCondCheckUnsat( Gia_Man_t * p );
extern int                 Gia_SweeperCheckEquiv( Gia_Man_t * p, int ProbeId1, int ProbeId2 );
extern Gia_Man_t *         Gia_SweeperExtractUserLogic( Gia_Man_t * p, Vec_Int_t * vProbeIds, Vec_Ptr_t * vInNames, Vec_Ptr_t * vOutNames );
extern void                Gia_SweeperLogicDump( Gia_Man_t * p, Vec_Int_t * vProbeIds, int fDumpConds, char * pFileName );
extern Gia_Man_t *         Gia_SweeperCleanup( Gia_Man_t * p, char * pCommLime );
extern Vec_Int_t *         Gia_SweeperGraft( Gia_Man_t * pDst, Vec_Int_t * vProbes, Gia_Man_t * pSrc );
extern int                 Gia_SweeperFraig( Gia_Man_t * p, Vec_Int_t * vProbeIds, char * pCommLime, int nWords, int nConfs, int fVerify, int fVerbose );
extern int                 Gia_SweeperRun( Gia_Man_t * p, Vec_Int_t * vProbeIds, char * pCommLime, int fVerbose );
/*=== giaSwitch.c ============================================================*/
extern float               Gia_ManEvaluateSwitching( Gia_Man_t * p );
extern float               Gia_ManComputeSwitching( Gia_Man_t * p, int nFrames, int nPref, int fProbOne );
extern Vec_Int_t *         Gia_ManComputeSwitchProbs( Gia_Man_t * pGia, int nFrames, int nPref, int fProbOne );
extern Vec_Int_t *         Gia_ManComputeSwitchProbs2( Gia_Man_t * pGia, int nFrames, int nPref, int fProbOne, int nRandPiFactor );
extern Vec_Flt_t *         Gia_ManPrintOutputProb( Gia_Man_t * p );
/*=== giaTim.c ===========================================================*/
extern int                 Gia_ManBoxNum( Gia_Man_t * p );
extern int                 Gia_ManRegBoxNum( Gia_Man_t * p );
extern int                 Gia_ManNonRegBoxNum( Gia_Man_t * p );
extern int                 Gia_ManBlackBoxNum( Gia_Man_t * p );
extern int                 Gia_ManBoxCiNum( Gia_Man_t * p );
extern int                 Gia_ManBoxCoNum( Gia_Man_t * p );
extern int                 Gia_ManClockDomainNum( Gia_Man_t * p );
extern int                 Gia_ManIsSeqWithBoxes( Gia_Man_t * p );
extern int                 Gia_ManIsNormalized( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManOrderWithBoxes( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupNormalize( Gia_Man_t * p, int fHashMapping );
extern Gia_Man_t *         Gia_ManDupUnnormalize( Gia_Man_t * p );
extern Gia_Man_t *         Gia_ManDupUnshuffleInputs( Gia_Man_t * p );
extern int                 Gia_ManLevelWithBoxes( Gia_Man_t * p );
extern int                 Gia_ManLutLevelWithBoxes( Gia_Man_t * p );
extern void *              Gia_ManUpdateTimMan( Gia_Man_t * p, Vec_Int_t * vBoxPres );
extern void *              Gia_ManUpdateTimMan2( Gia_Man_t * p, Vec_Int_t * vBoxesLeft, int nTermsDiff );
extern Gia_Man_t *         Gia_ManUpdateExtraAig( void * pTime, Gia_Man_t * pAig, Vec_Int_t * vBoxPres );
extern Gia_Man_t *         Gia_ManUpdateExtraAig2( void * pTime, Gia_Man_t * pAig, Vec_Int_t * vBoxesLeft );
extern Gia_Man_t *         Gia_ManDupCollapse( Gia_Man_t * p, Gia_Man_t * pBoxes, Vec_Int_t * vBoxPres, int fSeq );
extern int                 Gia_ManVerifyWithBoxes( Gia_Man_t * pGia, int nBTLimit, int nTimeLim, int fSeq, int fNameMap, int fDumpFiles, int fVerbose, char * pFileSpec );
extern Vec_Int_t *         Gia_ManDeriveBoxMapping( Gia_Man_t * pGia );
/*=== giaTruth.c ===========================================================*/
extern word                Gia_LutComputeTruth6( Gia_Man_t * p, int iObj, Vec_Wrd_t * vTruths );
extern word                Gia_ObjComputeTruthTable6Lut( Gia_Man_t * p, int iObj, Vec_Wrd_t * vTemp );
extern word                Gia_ObjComputeTruthTable6( Gia_Man_t * p, Gia_Obj_t * pObj, Vec_Int_t * vSupp, Vec_Wrd_t * vTruths );
extern word                Gia_ObjComputeTruth6Cis( Gia_Man_t * p, int iLit, Vec_Int_t * vSupp, Vec_Wrd_t * vTemp );
extern void                Gia_ObjCollectInternal( Gia_Man_t * p, Gia_Obj_t * pObj );
extern word *              Gia_ObjComputeTruthTable( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ObjComputeTruthTableStart( Gia_Man_t * p, int nVarsMax );
extern void                Gia_ObjComputeTruthTableStop( Gia_Man_t * p );
extern word *              Gia_ObjComputeTruthTableCut( Gia_Man_t * p, Gia_Obj_t * pObj, Vec_Int_t * vLeaves );
/*=== giaTsim.c ============================================================*/
extern Gia_Man_t *         Gia_ManReduceConst( Gia_Man_t * pAig, int fVerbose );
/*=== giaUtil.c ===========================================================*/
extern unsigned            Gia_ManRandom( int fReset );
extern word                Gia_ManRandomW( int fReset );
extern void                Gia_ManRandomInfo( Vec_Ptr_t * vInfo, int iInputStart, int iWordStart, int iWordStop );
extern char *              Gia_TimeStamp();
extern char *              Gia_FileNameGenericAppend( char * pBase, char * pSuffix );
extern Vec_Ptr_t *         Gia_GetFakeNames( int nNames, int fCaps );
extern void                Gia_ManIncrementTravId( Gia_Man_t * p );
extern void                Gia_ManCleanMark01( Gia_Man_t * p );
extern void                Gia_ManSetMark0( Gia_Man_t * p );
extern void                Gia_ManCleanMark0( Gia_Man_t * p );
extern void                Gia_ManCheckMark0( Gia_Man_t * p );
extern void                Gia_ManSetMark1( Gia_Man_t * p );
extern void                Gia_ManCleanMark1( Gia_Man_t * p );
extern void                Gia_ManCheckMark1( Gia_Man_t * p );
extern void                Gia_ManCleanValue( Gia_Man_t * p );
extern void                Gia_ManCleanLevels( Gia_Man_t * p, int Size );
extern void                Gia_ManCleanTruth( Gia_Man_t * p );
extern void                Gia_ManFillValue( Gia_Man_t * p );
extern void                Gia_ObjSetPhase( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ManSetPhase( Gia_Man_t * p );
extern void                Gia_ManSetPhasePattern( Gia_Man_t * p, Vec_Int_t * vCiValues );
extern void                Gia_ManSetPhase1( Gia_Man_t * p );
extern void                Gia_ManCleanPhase( Gia_Man_t * p );
extern int                 Gia_ManCheckCoPhase( Gia_Man_t * p );
extern int                 Gia_ManLevelNum( Gia_Man_t * p );
extern int                 Gia_ManLevelRNum( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManGetCiLevels( Gia_Man_t * p );
extern int                 Gia_ManSetLevels( Gia_Man_t * p, Vec_Int_t * vCiLevels );
extern Vec_Int_t *         Gia_ManReverseLevel( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManRequiredLevel( Gia_Man_t * p );
extern void                Gia_ManCreateValueRefs( Gia_Man_t * p );
extern void                Gia_ManCreateRefs( Gia_Man_t * p );
extern void                Gia_ManCreateLitRefs( Gia_Man_t * p );
extern int *               Gia_ManCreateMuxRefs( Gia_Man_t * p );
extern int                 Gia_ManCrossCut( Gia_Man_t * p, int fReverse );
extern Vec_Int_t *         Gia_ManCollectPoIds( Gia_Man_t * p );
extern int                 Gia_ObjIsMuxType( Gia_Obj_t * pNode );
extern int                 Gia_ObjRecognizeExor( Gia_Obj_t * pObj, Gia_Obj_t ** ppFan0, Gia_Obj_t ** ppFan1 );
extern Gia_Obj_t *         Gia_ObjRecognizeMux( Gia_Obj_t * pNode, Gia_Obj_t ** ppNodeT, Gia_Obj_t ** ppNodeE );
extern int                 Gia_ObjRecognizeMuxLits( Gia_Man_t * p, Gia_Obj_t * pNode, int * iLitT, int * iLitE );
extern int                 Gia_NodeMffcSize( Gia_Man_t * p, Gia_Obj_t * pNode );
extern int                 Gia_NodeMffcSizeMark( Gia_Man_t * p, Gia_Obj_t * pNode );
extern int                 Gia_NodeMffcSizeSupp( Gia_Man_t * p, Gia_Obj_t * pNode, Vec_Int_t * vSupp );
extern int                 Gia_NodeMffcMapping( Gia_Man_t * p );
extern int                 Gia_ManHasDangling( Gia_Man_t * p );
extern int                 Gia_ManMarkDangling( Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManGetDangling( Gia_Man_t * p );
extern void                Gia_ObjPrint( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ManPrint( Gia_Man_t * p );
extern void                Gia_ManPrintCo( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ManPrintCone( Gia_Man_t * p, Gia_Obj_t * pObj, int * pLeaves, int nLeaves, Vec_Int_t * vNodes );
extern void                Gia_ManPrintConeMulti( Gia_Man_t * p, Vec_Int_t * vObjs, Vec_Int_t * vLeaves, Vec_Int_t * vNodes );
extern void                Gia_ManPrintCone2( Gia_Man_t * p, Gia_Obj_t * pObj );
extern void                Gia_ManInvertConstraints( Gia_Man_t * pAig );
extern void                Gia_ManInvertPos( Gia_Man_t * pAig );
extern int                 Gia_ManCompare( Gia_Man_t * p1, Gia_Man_t * p2 );
extern void                Gia_ManMarkFanoutDrivers( Gia_Man_t * p );
extern void                Gia_ManSwapPos( Gia_Man_t * p, int i );
extern Vec_Int_t *         Gia_ManSaveValue( Gia_Man_t * p );
extern void                Gia_ManLoadValue( Gia_Man_t * p, Vec_Int_t * vValues );
extern Vec_Int_t *         Gia_ManFirstFanouts( Gia_Man_t * p );
extern int                 Gia_ManCheckSuppOverlap( Gia_Man_t * p, int iNode1, int iNode2 );
extern int                 Gia_ManCountPisWithFanout( Gia_Man_t * p );
extern int                 Gia_ManCountPosWithNonZeroDrivers( Gia_Man_t * p );
extern void                Gia_ManUpdateCopy( Vec_Int_t * vCopy, Gia_Man_t * p );
extern Vec_Int_t *         Gia_ManComputeDistance( Gia_Man_t * p, int iObj, Vec_Int_t * vObjs, int fVerbose );

/*=== giaTtopt.cpp ===========================================================*/
extern Gia_Man_t *         Gia_ManTtopt( Gia_Man_t * p, int nIns, int nOuts, int nRounds );
extern Gia_Man_t *         Gia_ManTtoptCare( Gia_Man_t * p, int nIns, int nOuts, int nRounds, char * pFileName, int nRarity );

/*=== giaTransduction.cpp ===========================================================*/
extern Gia_Man_t *         Gia_ManTransductionBdd( Gia_Man_t * pGia, int nType, int fMspf, int nRandom, int nSortType, int nPiShuffle, int nParameter, int fLevel, Gia_Man_t * pExdc, int fNewLine, int nVerbose );
extern Gia_Man_t *         Gia_ManTransductionTt( Gia_Man_t * pGia, int nType, int fMspf, int nRandom, int nSortType, int nPiShuffle, int nParameter, int fLevel, Gia_Man_t * pExdc, int fNewLine, int nVerbose );

/*=== giaRrr.cpp ===========================================================*/
extern Gia_Man_t *         Gia_ManRrr( Gia_Man_t *pGia, int iSeed, int nWords, int nTimeout, int nSchedulerVerbose, int nPartitionerVerbose, int nOptimizerVerbose, int nAnalyzerVerbose, int nSimulatorVerbose, int nSatSolverVerbose, int fUseBddCspf, int fUseBddMspf, int nConflictLimit, int nSortType, int nOptimizerFlow, int nSchedulerFlow, int nPartitionType, int nDistance, int nJobs, int nThreads, int nPartitionSize, int nPartitionSizeMin, int fDeterministic, int nParallelPartitions, int fOptOnInsert, int fGreedy );

/*=== giaCTas.c ===========================================================*/
typedef struct Tas_Man_t_  Tas_Man_t;
extern Tas_Man_t *         Tas_ManAlloc( Gia_Man_t * pAig, int nBTLimit );
extern void                Tas_ManStop( Tas_Man_t * p );
extern Vec_Int_t *         Tas_ReadModel( Tas_Man_t * p );
extern void                Tas_ManSatPrintStats( Tas_Man_t * p );
extern int                 Tas_ManSolve( Tas_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pObj2 );
extern int                 Tas_ManSolveArray( Tas_Man_t * p, Vec_Ptr_t * vObjs );

/*=== giaDecGraph.c ===========================================================*/
extern Gia_Man_t*          Gia_ManDecGraph( Gia_Man_t* p );
extern Gia_Man_t*          Gia_ManDecGraphFromFile( char* pFileName );

/*=== giaBound.c ===========================================================*/
typedef struct Bnd_Man_t_  Bnd_Man_t;

extern Bnd_Man_t*           Bnd_ManStart( Gia_Man_t *pSpec, Gia_Man_t *pImpl, int fVerbose );
extern void                 Bnd_ManStop();

// getter
extern int                  Bnd_ManGetNInternal();
extern int                  Bnd_ManGetNExtra();

//for fraig
extern void                 Bnd_ManMap( int iLit, int id, int spec );
extern void                 Bnd_ManMerge( int id1, int id2, int phaseDiff );
extern void                 Bnd_ManFinalizeMappings();
extern void                 Bnd_ManPrintMappings();
extern Gia_Man_t*           Bnd_ManStackGias( Gia_Man_t *pSpec, Gia_Man_t *pImpl );
extern int                  Bnd_ManCheckCoMerged( Gia_Man_t *p );

// for eco
extern int                  Bnd_ManCheckBound( Gia_Man_t *p, int fVerbose );
extern void                 Bnd_ManFindBound( Gia_Man_t *p, Gia_Man_t *pImpl );
extern Gia_Man_t*           Bnd_ManGenSpecOut( Gia_Man_t *p );
extern Gia_Man_t*           Bnd_ManGenImplOut( Gia_Man_t *p );
extern Gia_Man_t*           Bnd_ManGenPatched( Gia_Man_t *pOut, Gia_Man_t *pSpec, Gia_Man_t *pPatch );
extern Gia_Man_t*           Bnd_ManGenPatched1( Gia_Man_t *pOut, Gia_Man_t *pSpec );
extern Gia_Man_t*           Bnd_ManGenPatched2( Gia_Man_t *pImpl, Gia_Man_t *pPatch, int fSkiptStrash, int fVerbose );
extern void                 Bnd_ManSetEqOut( int eq );
extern void                 Bnd_ManSetEqRes( int eq );
extern void                 Bnd_ManPrintStats();

// util
extern Gia_Man_t*           Bnd_ManCutBoundary( Gia_Man_t *p, Vec_Int_t* vEI, Vec_Int_t* vEO, Vec_Bit_t* vEI_phase, Vec_Bit_t* vEO_phase );

extern int                  Gia_ObjCheckMffc( Gia_Man_t * p, Gia_Obj_t * pRoot, int Limit, Vec_Int_t * vNodes, Vec_Int_t * vLeaves, Vec_Int_t * vInners );

ABC_NAMESPACE_HEADER_END


#endif

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////

