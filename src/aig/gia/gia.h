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

// The package has two "none" sentinels. They differ by a single hex digit, and each is the
// maximum value of a differently sized bit field, so the two belong to separate domains.
//
// GIA_NONE is 0x1FFFFFFF == 2^29 - 1, exactly the largest value a 29-bit field can hold.
// It marks "no fanin" in the 29-bit offset fields iDiff0 and iDiff1 of Gia_Obj_t, so an
// absent fanin costs no extra flag bit: the offset field encodes its own absence. Five of
// the fourteen kind predicates below test it -- Gia_ObjIsCi, Gia_ObjIsCo, Gia_ObjIsAnd,
// Gia_ObjIsBuf and Gia_ObjIsConst0 -- and Gia_ManStart writes it into both offset fields
// of object 0 to create the constant-0 object (giaMan.c:L75).
//
// GIA_VOID is 0x0FFFFFFF == 2^28 - 1, exactly the largest value a 28-bit field can hold.
// It belongs to the 28-bit iRepr member of Gia_Rpr_t below and means "no representative"
// in the equivalence-class machinery, whose helpers -- Gia_ObjReprObj, Gia_ObjIsHead,
// Gia_ObjIsNone, Gia_ObjIsTail and Gia_ClassUndoPair -- key on it exclusively.
//
// The two do not cross domains cleanly, and the way they fail differs in each direction.
// Assigning GIA_NONE to the 28-bit iRepr truncates the value to that field's width and
// leaves the field holding exactly the GIA_VOID bit pattern, which iRepr's readers accept
// as "no representative". Assigning GIA_VOID to a 29-bit offset field truncates nothing
// and leaves an ordinary in-range offset, which the kind predicates read as a fanin at
// distance 2^28 - 1. Each constant is therefore used with the API of its own field.
#define GIA_NONE 0x1FFFFFFF
#define GIA_VOID 0x0FFFFFFF

////////////////////////////////////////////////////////////////////////
///                         BASIC TYPES                              ///
////////////////////////////////////////////////////////////////////////

// Gia_Dat_t is declared here and is never defined anywhere in src/. Exactly two lines of
// code mention it - this typedef and the pUData member of Gia_Man_t below - every other
// tree-wide hit being explanatory prose, and no code reads, writes, allocates or frees a
// Gia_Dat_t. Its intended contents are therefore not determinable from the source.
typedef struct Gia_MmFixed_t_        Gia_MmFixed_t;    
typedef struct Gia_MmFlex_t_         Gia_MmFlex_t;     
typedef struct Gia_MmStep_t_         Gia_MmStep_t;     
typedef struct Gia_Dat_t_            Gia_Dat_t;

// One equivalence-class record per object, held in the pReprs side array of the manager
// below and indexed by object identifier. The record is exactly one 32-bit word wide
// (measured sizeof(Gia_Rpr_t) == 4), which is why iRepr gets 28 bits and not 29: the four
// flag bits share the same word. The 28-bit width caps a representative index at
// 2^28 - 1, and that value is GIA_VOID, the "no representative" marker, so the largest
// index this field can actually name is 2^28 - 2 -- well below the manager's own ceiling of
// 2^29 objects.
typedef struct Gia_Rpr_t_ Gia_Rpr_t;
struct Gia_Rpr_t_
{
    unsigned       iRepr   : 28;  // representative node
    unsigned       fProved :  1;  // marks the proved equivalence
    unsigned       fFailed :  1;  // marks the failed equivalence
    unsigned       fColorA :  1;  // marks cone of A
    unsigned       fColorB :  1;  // marks cone of B
};

// One placement record per object, held in the pPlacement side array of the manager below.
// Also exactly one 32-bit word wide (measured sizeof(Gia_Plc_t) == 4): two 15-bit
// coordinates plus two flag bits fill the word, so each coordinate is limited to the range
// 0 .. 2^15 - 1. This record and Gia_Rpr_t are each one word, so either side array costs
// four bytes per object.
typedef struct Gia_Plc_t_ Gia_Plc_t;
struct Gia_Plc_t_
{
    unsigned       fFixed  :  1;  // the placement of this object is fixed
    unsigned       xCoord  : 15;  // x-ooordinate of the placement
    unsigned       fUndef  :  1;  // the placement of this object is not assigned
    unsigned       yCoord  : 15;  // y-ooordinate of the placement
};

// THE OBJECT. This is the one structure the whole package is built on, and it is exactly
// three 32-bit words: measured sizeof(Gia_Obj_t) == 12, with no padding. Four consequences
// follow from that budget:
//
//  * There is no type-tag field. Nothing in the object says "I am an AND" or "I am a
//    combinational input". Kind is inferred by the predicates further down from one flag
//    bit (fTerm), the GIA_NONE sentinel, and the relative ORDERING of the two offset
//    fields, with p->pMuxes consulted to separate a real MUX from a plain AND. See
//    Gia_ObjIsTerm .. Gia_ManObjIsConst0.
//  * Fanins are neither pointers nor absolute indices, but backward relative offsets in
//    whole Gia_Obj_t units, resolved by subtraction -- see Gia_ObjFanin0 and
//    Gia_ObjFaninId0. The field is unsigned and only subtracted, so no offset reaches
//    forward; a positive, well-formed one lands earlier, ordering the array topologically.
//  * An offset is position independent, so the stored fanins stay correct across the
//    ABC_REALLOC inside Gia_ManAppendObj, which may move the whole array. Absolute
//    pointers would not. The observable consequence is that a Gia_Obj_t * held across an
//    append may no longer name the same object once growth relocates the storage, whereas
//    an object identifier keeps naming the same position in the array.
//  * Objects carry no fanout links. Fanout is optional and lives outside the object, in
//    the pFanData or vFanout side arrays of the manager.
//
// Objects live in one flat array, p->pObjs, and an object's identifier is its index into
// that array (Gia_ObjId). Everything that does not fit in twelve bytes lives in a side
// table of the manager, and those tables use two different index domains. MUX control
// inputs, equivalence classes, levels, reference counts, traversal marks and placement are
// indexed by OBJECT IDENTIFIER, so entry i belongs to object i. Names are not: vNamesIn
// and vNamesOut are indexed by CI and CO list position, as Gia_ObjCiName and Gia_ObjCoName
// show, while vNamesNode is indexed by object identifier.
typedef struct Gia_Obj_t_ Gia_Obj_t;
struct Gia_Obj_t_
{
    // ---- word 1: the first fanin offset, plus the three flag bits that share its 32 bits.
    // Backward relative offset of fanin 0: Gia_ObjFanin0 returns pObj - pObj->iDiff0, and
    // Gia_ObjFaninId0 performs the same subtraction on identifiers. The field is 29 bits
    // wide, which is what is left of the word once the three flags below take their bit
    // each. Its all-ones value is reserved as GIA_NONE and means "no fanin" -- which is how
    // a combinational input and the constant-0 object are told apart without a type tag --
    // so the largest ordinary offset the field can express is 2^29 - 2. The manager's own
    // ceiling of 2^29 objects is enforced separately, on Gia_ManAppendObj's growth path.
    unsigned       iDiff0 :  29;  // the diff of the first fanin
    // Inversion carried on the EDGE to fanin 0, not on the fanin object. One inversion bit
    // per edge is what lets a single node type (a two-input conjunction) express any Boolean
    // function. Combined with the offset it reconstitutes a literal -- see Gia_ObjFaninLit0.
    unsigned       fCompl0:   1;  // the complemented attribute
    // General-purpose mark, and the most heavily reused bit in the package. It is owned by
    // whichever pass is running, so its meaning is not fixed by the declaration: it serves
    // as a plain user mark, as the low bit of the ternary-simulation state packed with
    // fMark1 (Gia_ObjTerSimSetC/Set0/Set1/SetX), as the low bit of the two-bit saturating
    // fanout count Gia_ManAppendAnd maintains while p->fSweeper is set, and as the delete
    // marker Gia_ManDupMarked consumes and clears (giaDup.c:L1554-1558). Two passes cannot
    // hold it at once.
    unsigned       fMark0 :   1;  // first user-controlled mark
    // The only genuine type bit in the object: set for combinational inputs and outputs and
    // clear for the constant-0 object and every internal node. The terminal and non-terminal
    // predicates key on it; Gia_ObjIsMuxId keys on p->pMuxes and Gia_ManObjIsConst0 on the
    // address instead. Setting it changes iDiff1's meaning -- see the note on iDiff1 below.
    unsigned       fTerm  :   1;  // terminal node (CI/CO)

    // ---- word 2: the second fanin offset, plus three more flag bits.
    // Backward relative offset of fanin 1 on an internal node, resolved by Gia_ObjFanin1
    // exactly as iDiff0 is. Overloaded: when fTerm is set this is not an offset at all but
    // the object's index in the manager's vCis or vCos identifier list, written by
    // Gia_ManAppendCi and Gia_ManAppendCo and read back by Gia_ObjCioId. On an internal node
    // its ordering against iDiff0 is the type tag, in three non-overlapping cases:
    // iDiff0 == iDiff1 is the buffer form; iDiff0 < iDiff1 is a real XOR; iDiff0 > iDiff1 is
    // AND-shaped, and telling a plain AND from a real MUX there needs p->pMuxes.
    unsigned       iDiff1 :  29;  // the diff of the second fanin
    // Inversion carried on the edge to fanin 1; see fCompl0. Left unused when fTerm is set,
    // because a terminal has at most one fanin.
    unsigned       fCompl1:   1;  // the complemented attribute
    // General-purpose mark with three of fMark0's four roles: a plain user mark, the high bit
    // of the ternary-simulation state pair, and the high bit of the saturating fanout count.
    unsigned       fMark1 :   1;  // second user-controlled mark
    // Nominally the value this node takes when every combinational input is 0. That is what
    // Gia_ManSetPhase produces (giaUtil.c:L517-523), by walking the whole array and calling
    // the per-object helper Gia_ObjSetPhase (giaUtil.c:L467-487) with the inputs left at 0.
    // The field is not exclusively that: Gia_ManSetPhasePattern (giaUtil.c:L524) leaves the
    // value under an arbitrary input pattern in it, Gia_ManSetPhase1 (giaUtil.c:L555) the
    // value under the all-ones pattern, and Gia_ManAppendAnd overwrites it with the
    // conjunction of its fanins' phases whenever p->fSweeper or p->fBuiltInSim is set.
    // Gia_ObjPhaseReal folds in the complement bit of a tagged address before returning it.
    unsigned       fPhase :   1;  // value under 000 pattern

    // ---- word 3: one full 32-bit word, no bit fields.
    // The one full-width member, and the object's general scratch word. During duplication
    // and rebuild it holds this object's copy literal in the DESTINATION manager, with all
    // ones as the "not yet copied" marker: Gia_ManFillValue writes ~0 into every Value
    // (giaUtil.c:L449-454) and the recursive duplicators test it as `if ( ~pObj->Value )
    // return;` (giaDup.c:L1856-1857), which is why Gia_ManCleanValue, writing 0 instead
    // (giaUtil.c:L423-424), does not serve that purpose. Outside a rebuild the field carries
    // whatever the running pass puts in it.
    unsigned       Value;         // application-specific value
};
// Value is currently used to store several types of information
// - pointer to the next node in the hash table during structural hashing
// - pointer to the node copy during duplication 
// The structural-hash chain is held outside the object: bucket heads live in the manager's
// vHTable and the chain links live in its vHash, an integer vector indexed by object
// identifier, as Gia_ManHashFind shows (giaHash.c:L83-97). Gia_ManAppendObj keeps that
// vector at one entry per object by pushing a zero whenever the table is live. Duplication
// stores a literal of the destination manager in Value, not an address.

// THE MANAGER. Read it as one flat object array plus a long tail of OPTIONAL side tables,
// not as a graph object. Four things carry the representation itself: pObjs/nObjs/nObjsAlloc,
// the identifier lists vCis and vCos, nRegs -- which splits each of those lists into its
// primary and its flop half and so decides every terminal's role -- and pMuxes, which holds
// the control input of every real MUX and is the only thing that tells one from a plain AND.
// Measured sizeof(Gia_Man_t) == 1136, nearly all of it null pointers on a freshly started
// manager.
//
// Three conventions run through the declaration:
//
//  * A side table that extends an object is indexed by OBJECT IDENTIFIER, so entry i belongs
//    to object i: pMuxes, pRefs, pLutRefs, pReprs, pNexts, pSibls, pIso (cecIso.c:L278-281),
//    pTravIds, pSwitching, pPlacement and vHash (giaHash.c:L86). That is why Gia_ManAppendObj
//    grows pMuxes and pushes onto vHash in lockstep with pObjs. Tables outside that list use
//    other domains -- vNamesIn and vNamesOut are indexed by CI and CO list position and
//    vCellMapping by literal -- so the rule holds for the members named here and no further.
//  * Derivable counts are not stored. There is no nPis, nPos or nAnds member: Gia_ManPiNum,
//    Gia_ManPoNum and Gia_ManAndNum compute them from nObjs, nRegs and the sizes of vCis and
//    vCos, which is what makes construction order load-bearing.
//  * Eight vectors are embedded BY VALUE rather than by pointer -- vHash, vHTable, vRefs,
//    vCopies, vCopies2, vCopiesTwo, vSuppVars and vVarMap. Code therefore takes their
//    address (&p->vHTable, &p->vHash) and Gia_ManStop releases them with Vec_IntErase
//    rather than Vec_IntFreeP (giaMan.c:L156-161, L185-187).
//
// Ownership is not uniform: some members are freed by Gia_ManStop, others are borrowed.
// Gia_ManMemory (giaMan.c:L239-256) totals the allocations it charges to the manager, which
// is a subset of what Gia_ManStop releases rather than a full ownership list. Members that
// are declared and released but never assigned on a Gia_Man_t anywhere in src/ are marked
// "not determinable" below rather than guessed at.
// new AIG manager
typedef struct Gia_Man_t_ Gia_Man_t;
struct Gia_Man_t_
{
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
    // Optional built-in simulator, started by Gia_ManBuiltInSimStart on a manager that has no
    // AND objects yet -- its only structural precondition is assert( Gia_ManAndNum(p) == 0 ),
    // so combinational inputs and outputs may already exist (giaSim.c:L776-796) -- and driven
    // from inside Gia_ManAppendAnd while fBuiltInSim is set, so that a node is simulated as it
    // is created. Simulation values live outside the objects, in vSims, as nSimWords 64-bit
    // words per object: Gia_ManObjSim indexes that vector as p->nSimWords * iObj
    // (giaGen.c:L108). Patterns are packed into those words, iPatsPi being the next slot to
    // fill out of nSimWords * 64.
    // Six members of this block are not determinable from the source and are marked one by one
    // below: nSimWordsT has no code reference outside its own declaration; vSimsT,
    // vClassOld, vClassNew and vPats are freed by Gia_ManStop but never assigned on a Gia_Man_t
    // anywhere in src/; and vPolars is written only from outside this package and never read.
    // Five of those names also occur on unrelated structs, which say nothing about these fields.
    int            fBuiltInSim;   // simulate each AND as it is appended
    int            iPatsPi;       // next pattern slot, out of 64 * nSimWords
    int            nSimWords;     // simulation words per object -- the stride into vSims
    int            nSimWordsT;    // not determinable: no code reference outside this line
    int            iPastPiMax;    // wrapping slot counter, bounded by 64 * nSimWordsMax
    int            nSimWordsMax;  // ceiling on nSimWords; set to 8 at start-up
    Vec_Wrd_t *    vSims;         // simulation values, nSimWords words per object
    Vec_Wrd_t *    vSimsT;        // not determinable: freed but never assigned on Gia_Man_t
    Vec_Wrd_t *    vSimsPi;       // input-side words, nSimWords per CI (giaSim.c:L788-789)
    Vec_Wrd_t *    vSimsPo;       // output-side words, computed or loaded (abc.c:L37125, L37138)
    Vec_Int_t *    vClassOld;     // not determinable: freed but never assigned on Gia_Man_t
    Vec_Int_t *    vClassNew;     // not determinable: freed but never assigned on Gia_Man_t
    Vec_Int_t *    vPats;         // not determinable: freed but never assigned on Gia_Man_t
    Vec_Bit_t *    vPolars;       // not determinable: written only outside this package
    // incremental simulation
    // A second, incremental mode, started by Gia_ManIncrSimStart (giaSim.c:L1150-1163). It
    // shares vSims, nSimWords and iPatsPi with the block above but replaces whole-array
    // re-simulation with a generation counter: iTimeStamp is bumped per update and
    // vTimeStamps records, per object identifier, the stamp at which that object was last
    // refreshed (giaSim.c:L1178, L1185), so Gia_ManIncrSimCone_rec can stop as soon as it meets
    // an object already at the current stamp (giaSim.c:L1196). That is the same O(1)-invalidation
    // idea as nTravIds/pTravIds above.
    int            fIncrSim;      // incremental simulation is running
    int            iNextPi;       // first CI not yet seeded, advanced at giaSim.c:L1141-1148
    int            iTimeStamp;    // current generation; bumped by each incremental update
    Vec_Int_t *    vTimeStamps;   // per-object generation of its last refresh
    // truth table computation for small functions
    // Scratch state for evaluating a small cone as an explicit truth table rather than by
    // simulation. nTtWords is the word count that goes with nTtVars, and vTtNums maps an object
    // identifier to that object's slot, through Gia_ObjNumId/Gia_ObjSetNumId below; the "no
    // slot" value there is -ABC_INFINITY, which is what Gia_ObjHasNumId tests for.
    int            nTtVars;       // truth table variables
    int            nTtWords;      // truth table words
    Vec_Int_t *    vTtNums;       // object numbers
    Vec_Int_t *    vTtNodes;      // internal nodes
    Vec_Ptr_t *    vTtInputs;     // truth tables for constant and primary inputs
    Vec_Wrd_t *    vTtMemory;     // truth tables for internal nodes
    // balancing
    // Two reusable work vectors for the AND/XOR balancing passes: the flattened supergate
    // currently being rebalanced, and a scratch store for the nodes it produces. They hang off
    // the manager rather than off a per-pass structure, so one allocation serves every call.
    Vec_Int_t *    vSuper;        // supergate
    Vec_Int_t *    vStore;        // node storage  
    // existential quantification
    // State for the quantification passes. vSuppWords holds nSuppWords support words per object
    // -- Gia_ManQuantInfoId indexes it as p->nSuppWords * iObj (giaExist.c:L46) -- and
    // Gia_ManAppendAnd calls Gia_ManQuantSetSuppAnd for every new AND while the member is
    // non-null, so support is maintained during construction rather than recomputed afterwards.
    int            iSuppPi;       // the number of support variables
    int            nSuppWords;    // the number of support words
    Vec_Wrd_t *    vSuppWords;    // support information
    Vec_Int_t      vCopiesTwo;    // intermediate copies
    Vec_Int_t      vSuppVars;     // used variables
    Vec_Int_t      vVarMap;       // used variables
    Gia_Dat_t *    pUData;        // not determinable: see the Gia_Dat_t note near the top
    // retiming data
    // Two per-object flag vectors, in Vec_Str_t form so that entry i belongs to object i,
    // marking objects across which retiming is blocked in the forward and the backward
    // direction. They are produced by Gia_ManRetimableF and Gia_ManRetimableB
    // (giaMini.c:L1378-1381) and consumed as retiming barriers by the SIF timing check
    // (giaSif.c:L500-502, L516-519).
    Vec_Str_t *    vStopsF;       // per-object forward retiming barrier
    Vec_Str_t *    vStopsB;       // per-object backward retiming barrier
    // iteration with boxes
    // Four precomputed boundaries that let the box-aware iterators at the end of this file
    // walk only the logic BETWEEN the box interface objects, which is what a hierarchical
    // design carrying a Tim_Man_t timing manager needs. All four are set together in one
    // place, from that timing manager's own primary-input and primary-output counts
    // (giaNf.c:L2721-2724); without a timing manager they collapse to plain CI/CO counts. The
    // first two index the vCis/vCos identifier lists, the last two index the object array:
    //   Gia_ManForEachCiIdWithBoxes        stops before iFirstNonPiId
    //   Gia_ManForEachCoWithBoxes          starts at iFirstPoId
    //   Gia_ManForEachObjWithBoxes         runs iFirstAndObj .. iFirstPoObj - 1
    //   Gia_ManForEachObjReverseWithBoxes  runs iFirstPoObj - 1 down to iFirstAndObj
    int            iFirstNonPiId; // first CI-list index that is not a true primary input
    int            iFirstPoId;    // first CO-list index that is a true primary output
    int            iFirstAndObj;  // first object identifier past the box input interface
    int            iFirstPoObj;   // first object identifier of the box output interface
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

// LITERALS AND TAGGED ADDRESSES. GIA carries TWO parallel complementation conventions, and
// telling them apart is the first thing to get straight when reading this header.
//
//  (1) The literal convention, used by everything that appends or hashes. A literal is an
//      integer, id << 1 | compl, built with Abc_Var2Lit and taken apart with Abc_Lit2Var and
//      Abc_LitIsCompl. Literal 0 is constant 0 and literal 1 is constant 1, which is why the
//      five helpers immediately below need no manager argument at all. The six constructors
//      that write an object directly -- Ci, And, XorReal, MuxReal, Buf and Co -- return
//      Gia_ObjId(p, pObj) << 1, an uncomplemented literal naming that object. The composite
//      and folding constructors return whatever their combination produces, which may be
//      complemented and, for the folding forms, may be an existing or a constant literal.
//  (2) The tagged-address convention, used by the Gia_Obj_t * accessors. Here the complement
//      bit rides in the low bit of the address itself. Every member of Gia_Obj_t is an
//      unsigned, so the type's alignment is that of unsigned -- 4 in this build -- and the
//      low bit of an address into the object array is therefore always 0 and free to carry
//      the tag. Gia_Regular strips the bit, Gia_Not flips it, Gia_NotCond flips it
//      conditionally, Gia_IsComplement reads it. An address that may be tagged has to pass
//      through Gia_Regular before any field is touched.
//
// Gia_Obj2Lit and Gia_Lit2Obj further down are the explicit bridge between the two.
static inline int          Gia_ManConst0Lit()                  { return 0; }
static inline int          Gia_ManConst1Lit()                  { return 1; }
static inline int          Gia_ManIsConst0Lit( int iLit )      { return (iLit == 0); }
static inline int          Gia_ManIsConst1Lit( int iLit )      { return (iLit == 1); }
static inline int          Gia_ManIsConstLit( int iLit )       { return (iLit <= 1); }

static inline Gia_Obj_t *  Gia_Regular( Gia_Obj_t * p )        { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) & ~01);                           }
static inline Gia_Obj_t *  Gia_Not( Gia_Obj_t * p )            { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) ^  01);                           }
static inline Gia_Obj_t *  Gia_NotCond( Gia_Obj_t * p, int c ) { return (Gia_Obj_t *)((ABC_PTRUINT_T)(p) ^ (c));                           }
static inline int          Gia_IsComplement( Gia_Obj_t * p )   { return (int)((ABC_PTRUINT_T)(p) & 01);                                    }

// DERIVED COUNTS. Of the graph cardinalities read here, only nObjs, nRegs, nXors, nMuxes,
// nBufs and nConstrs are stored fields; the rest are computed on demand from those and from
// the sizes of the two identifier lists. (The manager keeps plenty of other counters -- for
// simulation strides, traversal generations, truth-table widths and hash statistics -- which
// this block does not read.) Three of the derived counts repay a close reading:
//   Gia_ManPiNum subtracts nRegs from the CI count, so the primary inputs are exactly the CIs
//     that PRECEDE the flop outputs in vCis. The partition is positional, not tagged, which is
//     what makes construction order load-bearing throughout the package.
//   Gia_ManAndNum subtracts the CI count, the CO count and one from nObjs; the one is object 0,
//     the constant. It therefore counts buffers, real XORs and real MUXes as ANDs too, which is
//     why Gia_ManAndNotBufNum exists alongside it.
//   Gia_ManHasChoices is a null test on the pSibls side array, while Gia_ManChoiceNum scans that
//     whole array, so the second is O(nObjs) and not a stored counter.
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

// OBJECT GETTERS. Gia_ManObj is the bounds-checked index into the flat array; the rest are
// wrappers over the two identifier lists. Two of them are easy to misread:
//   Gia_ManConst1 does not return a second object. It returns Gia_Not of object 0 -- the same
//     object, with the complement bit set in the returned address (convention 2 above). There
//     is exactly one constant object in a manager.
//   Gia_ManRo and Gia_ManRi index the TAILS of the CI and CO lists, at Gia_ManPiNum(p)+v and
//     Gia_ManPoNum(p)+v, which is the same positional partition Gia_ManPiNum depends on.
static inline Gia_Obj_t *  Gia_ManConst0( Gia_Man_t * p )      { return p->pObjs;                                                          }
static inline Gia_Obj_t *  Gia_ManConst1( Gia_Man_t * p )      { return Gia_Not(Gia_ManConst0(p));                                         }
static inline Gia_Obj_t *  Gia_ManObj( Gia_Man_t * p, int v )  { assert( v >= 0 && v < p->nObjs ); return p->pObjs + v;                    }
static inline Gia_Obj_t *  Gia_ManCi( Gia_Man_t * p, int v )   { return Gia_ManObj( p, Vec_IntEntry(p->vCis,v) );                          }
static inline Gia_Obj_t *  Gia_ManCo( Gia_Man_t * p, int v )   { return Gia_ManObj( p, Vec_IntEntry(p->vCos,v) );                          }
static inline Gia_Obj_t *  Gia_ManPi( Gia_Man_t * p, int v )   { assert( v < Gia_ManPiNum(p) );  return Gia_ManCi( p, v );                 }
static inline Gia_Obj_t *  Gia_ManPo( Gia_Man_t * p, int v )   { assert( v < Gia_ManPoNum(p) );  return Gia_ManCo( p, v );                 }
static inline Gia_Obj_t *  Gia_ManRo( Gia_Man_t * p, int v )   { assert( v < Gia_ManRegNum(p) ); return Gia_ManCi( p, Gia_ManPiNum(p)+v ); }
static inline Gia_Obj_t *  Gia_ManRi( Gia_Man_t * p, int v )   { assert( v < Gia_ManRegNum(p) ); return Gia_ManCo( p, Gia_ManPoNum(p)+v ); }

// IDENTITY, VALUE, PHASE AND NAMES.
//   Gia_ObjId is pointer subtraction against the array base, guarded by a range assertion. An
//     object identifier is meaningful only relative to the manager that owns it, and it keeps
//     naming the same array position whatever happens to the storage; a Gia_Obj_t * may stop
//     naming its object if an append relocates the array, see Gia_ManAppendObj.
//   Gia_ObjCioId and Gia_ObjSetCioId are the terminal overload of iDiff1. Both assert fTerm
//     first, because on a non-terminal that same field is a fanin offset.
//   Gia_ObjPhase reads the bit as stored; Gia_ObjPhaseReal accepts a possibly tagged address and
//     folds the complement bit into the answer; Gia_ObjPhaseDiff compares two objects by id.
//   The four name accessors are null-safe lookups into the optional vNamesIn/vNamesOut/
//     vNamesNode vectors and return NULL when the manager carries no names. Their index domains
//     differ: Gia_ObjCiName and Gia_ObjCoName take CI and CO list positions, while Gia_ObjName
//     takes an object identifier -- Gia_ObjNameObj is the same lookup from an address.
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

// NODE KINDS -- THE FOURTEEN PREDICATES. Gia_Obj_t has NO type-tag field, so kind is inferred
// here from the fTerm flag, the GIA_NONE sentinel in the offset fields and the relative
// ORDERING of those two offsets, with p->pMuxes needed for the one case the object cannot
// settle. The whole decision is this:
//
//   fTerm  iDiff0    iDiff1 vs iDiff0  kind
//   -----  --------  ----------------  ---------------------------------------------------
//   1      GIA_NONE  (CIO index)       combinational input
//   1      offset    (CIO index)       combinational output
//   0      GIA_NONE  GIA_NONE          constant 0 -- always object 0
//   0      offset    equal             buffer
//   0      offset    greater           real XOR
//   0      offset    less              plain AND, or real MUX if p->pMuxes[Id] is positive
//
// DISPATCH ORDER MATTERS. Gia_ObjIsAnd is true for buffers, for real XORs and for real MUXes
// alike, because all three are non-terminal objects that have a fanin. A chain of tests must
// therefore take the narrow kinds before the wide one: buffer, then XOR, then MUX, then plain
// AND, which is the order Gia_ManDupMarked uses (giaDup.c:L1560-1569).
static inline int          Gia_ObjIsTerm( Gia_Obj_t * pObj )                   { return pObj->fTerm;                             } 
static inline int          Gia_ObjIsAndOrConst0( Gia_Obj_t * pObj )            { return!pObj->fTerm;                             } 
static inline int          Gia_ObjIsCi( Gia_Obj_t * pObj )                     { return pObj->fTerm && pObj->iDiff0 == GIA_NONE; } 
static inline int          Gia_ObjIsCo( Gia_Obj_t * pObj )                     { return pObj->fTerm && pObj->iDiff0 != GIA_NONE; } 
static inline int          Gia_ObjIsAnd( Gia_Obj_t * pObj )                    { return!pObj->fTerm && pObj->iDiff0 != GIA_NONE; } 
// The offset ordering IS the real-XOR tag; nothing else in the object marks one. Unlike
// Gia_ObjIsMux this needs no manager pointer, because the evidence is inside the object.
static inline int          Gia_ObjIsXor( Gia_Obj_t * pObj )                    { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 < pObj->iDiff1; } 
static inline int          Gia_ObjIsMuxId( Gia_Man_t * p, int iObj )           { return p->pMuxes && p->pMuxes[iObj] > 0;        } 
static inline int          Gia_ObjIsMux( Gia_Man_t * p, Gia_Obj_t * pObj )     { return Gia_ObjIsMuxId( p, Gia_ObjId(p, pObj) ); } 
// A real MUX object is BIT-IDENTICAL to a plain AND, so this predicate has to consult p->pMuxes
// through Gia_ObjIsMux to exclude one. "Real" throughout this header means the single-object
// form of XOR or MUX, as opposed to the structural form built out of several AND objects.
static inline int          Gia_ObjIsAndReal( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 > pObj->iDiff1 && !Gia_ObjIsMux(p, pObj); } 
// Equal offsets, neither of them the sentinel, and not a terminal. With p->fGiaSimple set,
// Gia_ManAppendAnd accepts AND(x, x) and writes two equal offsets for it, and this predicate
// reports that object as a buffer.
static inline int          Gia_ObjIsBuf( Gia_Obj_t * pObj )                    { return pObj->iDiff0 == pObj->iDiff1 && pObj->iDiff0 != GIA_NONE && !pObj->fTerm;    } 
// Exists precisely because Gia_ObjIsAnd is true for buffers as well: this one accepts plain
// ANDs, real XORs and real MUXes but rejects the equal-offset buffer case.
static inline int          Gia_ObjIsAndNotBuf( Gia_Obj_t * pObj )              { return Gia_ObjIsAnd(pObj) && pObj->iDiff0 != pObj->iDiff1; } 
static inline int          Gia_ObjIsCand( Gia_Obj_t * pObj )                   { return Gia_ObjIsAnd(pObj) || Gia_ObjIsCi(pObj); } 
static inline int          Gia_ObjIsConst0( Gia_Obj_t * pObj )                 { return pObj->iDiff0 == GIA_NONE && pObj->iDiff1 == GIA_NONE;     } 
static inline int          Gia_ManObjIsConst0( Gia_Man_t * p, Gia_Obj_t * pObj){ return pObj == p->pObjs;                        } 

// THE BRIDGE between the two conventions. Gia_Obj2Lit strips the tag off an address, turns the
// object into an identifier and re-attaches the bit as the literal's low bit; Gia_Lit2Obj does
// the reverse. Gia_ManCiLit is the same conversion starting from a CI list position.
static inline int          Gia_Obj2Lit( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Abc_Var2Lit(Gia_ObjId(p, Gia_Regular(pObj)), Gia_IsComplement(pObj)); }
static inline Gia_Obj_t *  Gia_Lit2Obj( Gia_Man_t * p, int iLit )              { return Gia_NotCond(Gia_ManObj(p, Abc_Lit2Var(iLit)), Abc_LitIsCompl(iLit));  }
static inline int          Gia_ManCiLit( Gia_Man_t * p, int CiId )             { return Gia_Obj2Lit( p, Gia_ManCi(p, CiId) );                }

// Conversions between the two index domains, object identifier and CI/CO list position.
// Gia_ManIdToCioId reaches Gia_ObjCioId and so inherits its fTerm assertion: it is defined for
// terminals only.
static inline int          Gia_ManIdToCioId( Gia_Man_t * p, int Id )           { return Gia_ObjCioId( Gia_ManObj(p, Id) );                   }
static inline int          Gia_ManCiIdToId( Gia_Man_t * p, int CiId )          { return Gia_ObjId( p, Gia_ManCi(p, CiId) );                  }
static inline int          Gia_ManCoIdToId( Gia_Man_t * p, int CoId )          { return Gia_ObjId( p, Gia_ManCo(p, CoId) );                  }

// TERMINAL ROLES. Whether a terminal is a primary input, a primary output, a flop output or a
// flop input is not a stored bit either: it is a range test on the CI/CO list position against
// Gia_ManPiNum or Gia_ManPoNum. These four are therefore correct only while every primary input
// precedes every flop output in vCis and every primary output precedes every flop input in
// vCos, which is an ordering the construction order has to establish.
static inline int          Gia_ObjIsPi( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCi(pObj) && Gia_ObjCioId(pObj) < Gia_ManPiNum(p);   } 
static inline int          Gia_ObjIsPo( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCo(pObj) && Gia_ObjCioId(pObj) < Gia_ManPoNum(p);   } 
static inline int          Gia_ObjIsRo( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCi(pObj) && Gia_ObjCioId(pObj) >= Gia_ManPiNum(p);  } 
static inline int          Gia_ObjIsRi( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjIsCo(pObj) && Gia_ObjCioId(pObj) >= Gia_ManPoNum(p);  } 

// FLOP PAIRING. A flop output and its flop input sit at mirrored positions in the two lists:
// flop k is CI number Gia_ManPiNum(p)+k and CO number Gia_ManPoNum(p)+k, and the two offset
// expressions here are that identity rearranged. Each asserts the role of its argument first,
// so neither is usable on a primary input or output.
static inline Gia_Obj_t *  Gia_ObjRoToRi( Gia_Man_t * p, Gia_Obj_t * pObj )    { assert( Gia_ObjIsRo(p, pObj) ); return Gia_ManCo(p, Gia_ManCoNum(p) - Gia_ManCiNum(p) + Gia_ObjCioId(pObj)); } 
static inline Gia_Obj_t *  Gia_ObjRiToRo( Gia_Man_t * p, Gia_Obj_t * pObj )    { assert( Gia_ObjIsRi(p, pObj) ); return Gia_ManCi(p, Gia_ManCiNum(p) - Gia_ManCoNum(p) + Gia_ObjCioId(pObj)); } 
static inline int          Gia_ObjRoToRiId( Gia_Man_t * p, int ObjId )         { return Gia_ObjId( p, Gia_ObjRoToRi( p, Gia_ManObj(p, ObjId) ) );    } 
static inline int          Gia_ObjRiToRoId( Gia_Man_t * p, int ObjId )         { return Gia_ObjId( p, Gia_ObjRiToRo( p, Gia_ManObj(p, ObjId) ) );    }

// FANIN ACCESS.  A fanin is stored neither as an address nor as an absolute
// index.  On an internal node, iDiff0 and iDiff1 hold backward offsets, counted
// in whole Gia_Obj_t units, from the object down to each of its fanins, so every
// reader in this block is one subtraction.  Gia_ObjFanin0 subtracts in the
// address domain and Gia_ObjFaninId0 subtracts in the identifier domain; the two
// are the same arithmetic applied to two representations of the same position.
// The offset reading of iDiff1 holds for internal nodes only.  On a terminal that
// field is the CI/CO list index instead (Gia_ObjCioId), so a combinational output
// has just one meaningful fanin, reached through the iDiff0 readers, and a
// combinational input has none.  The fanin-1 readers below do not test fTerm.
// Two properties follow from subtraction alone.  First, the field is unsigned
// and only ever subtracted, so no offset reaches forward; a positive,
// well-formed offset lands on an earlier object, which is what puts the array
// in topological order, while a zero offset would name the object itself.
// Second, an offset stays correct when the whole array moves, which is what
// lets Gia_ManAppendObj reallocate p->pObjs without rewriting any object.
// Naming across the block: a trailing 0, 1 or 2 selects the fanin; the C forms
// read that fanin's complement bit; Id returns an identifier, Lit returns a
// literal, Child returns a tagged address; and a trailing p means the object's
// own identifier is recovered internally instead of being passed in.  The
// forms that take a separate int n select fanin 0 or 1 only, so the third
// fanin is not reachable through them.  Gia_ObjDiff0 and Gia_ObjDiff1 below
// return the raw stored offset with no subtraction applied; Gia_ObjFaninC0 and
// Gia_ObjFaninC1 return the complement bit that travels with each offset in
// the same 32-bit word.
static inline int          Gia_ObjDiff0( Gia_Obj_t * pObj )                    { return pObj->iDiff0;         }
static inline int          Gia_ObjDiff1( Gia_Obj_t * pObj )                    { return pObj->iDiff1;         }
static inline int          Gia_ObjFaninC0( Gia_Obj_t * pObj )                  { return pObj->fCompl0;        }
static inline int          Gia_ObjFaninC1( Gia_Obj_t * pObj )                  { return pObj->fCompl1;        }
// The third fanin does not live in the object.  A real MUX occupies the same
// twelve bytes as any other node, so its control input is held in the parallel
// side array p->pMuxes, indexed by object identifier and stored as a literal.
// Both readers below test only that p->pMuxes exists; neither tests that the
// object is a MUX.  On a manager that carries the side array the stored
// literal is 0 for every object that was not created as a real MUX, so
// Gia_ObjFaninC2 reports no complement and Gia_ObjFanin2 maps literal 0 to
// object 0, the constant.  Gia_ObjFanin2 returns a null address only when the
// manager has no side array at all.
static inline int          Gia_ObjFaninC2( Gia_Man_t * p, Gia_Obj_t * pObj )   { return p->pMuxes && Abc_LitIsCompl(p->pMuxes[Gia_ObjId(p, pObj)]);  }
static inline int          Gia_ObjFaninC( Gia_Obj_t * pObj, int n )            { return n ? Gia_ObjFaninC1(pObj) : Gia_ObjFaninC0(pObj);             }
static inline Gia_Obj_t *  Gia_ObjFanin0( Gia_Obj_t * pObj )                   { return pObj - pObj->iDiff0;  }
static inline Gia_Obj_t *  Gia_ObjFanin1( Gia_Obj_t * pObj )                   { return pObj - pObj->iDiff1;  }
static inline Gia_Obj_t *  Gia_ObjFanin2( Gia_Man_t * p, Gia_Obj_t * pObj )    { return p->pMuxes ? Gia_ManObj(p, Abc_Lit2Var(p->pMuxes[Gia_ObjId(p, pObj)])) : NULL;  }
static inline Gia_Obj_t *  Gia_ObjFanin( Gia_Obj_t * pObj, int n )             { return n ? Gia_ObjFanin1(pObj) : Gia_ObjFanin0(pObj);               }
// The Child readers return a tagged address rather than a plain one: the
// fanin's address with its complement bit folded into the low bit, in the
// convention described above Gia_Regular.  A tagged address is passed through
// Gia_Regular before it is dereferenced.  Gia_ObjChild2 composes
// Gia_ObjFanin2 with Gia_ObjFaninC2 and therefore carries their behaviour on
// objects that are not real MUXes.
static inline Gia_Obj_t *  Gia_ObjChild0( Gia_Obj_t * pObj )                   { return Gia_NotCond( Gia_ObjFanin0(pObj), Gia_ObjFaninC0(pObj) ); }
static inline Gia_Obj_t *  Gia_ObjChild1( Gia_Obj_t * pObj )                   { return Gia_NotCond( Gia_ObjFanin1(pObj), Gia_ObjFaninC1(pObj) ); }
static inline Gia_Obj_t *  Gia_ObjChild2( Gia_Man_t * p, Gia_Obj_t * pObj )    { return Gia_NotCond( Gia_ObjFanin2(p, pObj), Gia_ObjFaninC2(p, pObj) ); }
// The identifier forms take the object's own identifier as a second argument,
// which is what makes them usable in code that walks identifiers and never
// materialises a Gia_Obj_t address.  Nothing in the bodies checks that ObjId
// belongs to pObj: the subtraction is performed on whatever number is passed.
static inline int          Gia_ObjFaninId0( Gia_Obj_t * pObj, int ObjId )      { return ObjId - pObj->iDiff0;    }
static inline int          Gia_ObjFaninId1( Gia_Obj_t * pObj, int ObjId )      { return ObjId - pObj->iDiff1;    }
// The four third-fanin readers in the identifier and literal domains report a
// miss as -1, and they reach that value from two different conditions: the
// manager has no p->pMuxes, or it has one and the stored literal for this
// object is 0.  This is a different miss value from the one Gia_ObjFanin2 uses
// in the address domain, where a null address is returned for the first
// condition and object 0 for the second.
static inline int          Gia_ObjFaninId2( Gia_Man_t * p, int ObjId )         { return (p->pMuxes && p->pMuxes[ObjId]) ? Abc_Lit2Var(p->pMuxes[ObjId]) : -1; }
static inline int          Gia_ObjFaninId( Gia_Obj_t * pObj, int ObjId, int n ){ return n ? Gia_ObjFaninId1(pObj, ObjId) : Gia_ObjFaninId0(pObj, ObjId);      }
// The p forms recover the object's identifier through Gia_ObjId, which costs
// one further subtraction and one range assertion but removes the chance of
// passing a mismatched identifier.  They take the manager because Gia_ObjId
// needs the base of the array.
static inline int          Gia_ObjFaninId0p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjFaninId0( pObj, Gia_ObjId(p, pObj) );              }
static inline int          Gia_ObjFaninId1p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return Gia_ObjFaninId1( pObj, Gia_ObjId(p, pObj) );              }
static inline int          Gia_ObjFaninId2p( Gia_Man_t * p, Gia_Obj_t * pObj ) { return (p->pMuxes && p->pMuxes[Gia_ObjId(p, pObj)]) ? Abc_Lit2Var(p->pMuxes[Gia_ObjId(p, pObj)]) : -1; }
static inline int          Gia_ObjFaninIdp( Gia_Man_t * p, Gia_Obj_t * pObj, int n){ return n ? Gia_ObjFaninId1p(p, pObj) : Gia_ObjFaninId0p(p, pObj);     }
// The literal forms fold the complement bit into the returned value, producing
// the same identifier-and-inversion encoding that every constructor consumes.
// They are the form used when a fanin is about to be handed straight back to a
// hashing or append entry point, which takes literals rather than identifiers.
static inline int          Gia_ObjFaninLit0( Gia_Obj_t * pObj, int ObjId )     { return Abc_Var2Lit( Gia_ObjFaninId0(pObj, ObjId), Gia_ObjFaninC0(pObj) ); }
static inline int          Gia_ObjFaninLit1( Gia_Obj_t * pObj, int ObjId )     { return Abc_Var2Lit( Gia_ObjFaninId1(pObj, ObjId), Gia_ObjFaninC1(pObj) ); }
static inline int          Gia_ObjFaninLit2( Gia_Man_t * p, int ObjId )        { return (p->pMuxes && p->pMuxes[ObjId]) ? p->pMuxes[ObjId] : -1;           }
static inline int          Gia_ObjFaninLit( Gia_Obj_t * pObj, int ObjId, int n ){ return n ? Gia_ObjFaninLit1(pObj, ObjId) : Gia_ObjFaninLit0(pObj, ObjId);}
static inline int          Gia_ObjFaninLit0p( Gia_Man_t * p, Gia_Obj_t * pObj) { return Abc_Var2Lit( Gia_ObjFaninId0p(p, pObj), Gia_ObjFaninC0(pObj) );    }
static inline int          Gia_ObjFaninLit1p( Gia_Man_t * p, Gia_Obj_t * pObj) { return Abc_Var2Lit( Gia_ObjFaninId1p(p, pObj), Gia_ObjFaninC1(pObj) );    }
static inline int          Gia_ObjFaninLit2p( Gia_Man_t * p, Gia_Obj_t * pObj) { return (p->pMuxes && p->pMuxes[Gia_ObjId(p, pObj)]) ? p->pMuxes[Gia_ObjId(p, pObj)] : -1;    }
static inline int          Gia_ObjFaninLitp( Gia_Man_t * p, Gia_Obj_t * pObj, int n ){ return n ? Gia_ObjFaninLit1p(p, pObj) : Gia_ObjFaninLit0p(p, pObj);}
// Toggles the complement bit on a combinational output's single driver edge,
// asserting Gia_ObjIsCo first.  Only fCompl0 changes; the offsets are
// untouched, so the topological order and the object kind are unaffected, and a
// combinational output is nobody's fanin.  Gia_ManPatchCoDriver below also
// writes fCompl0 in place, together with iDiff0, to redirect a driver edge.
static inline void         Gia_ObjFlipFaninC0( Gia_Obj_t * pObj )              { assert( Gia_ObjIsCo(pObj) ); pObj->fCompl0 ^= 1;          }
// Fanin count by kind: 3 for a real MUX, 2 for an AND, 1 for a combinational
// output, 0 otherwise.  It takes the manager because deciding MUX-ness needs
// p->pMuxes, and it tests MUX first because a real MUX also answers yes to
// Gia_ObjIsAnd.  There is no buffer case, and Gia_ObjIsAnd is true for a
// buffer, so a buffer is reported as having two fanins - which is consistent
// with the encoding, since a buffer stores two equal offsets.
static inline int          Gia_ObjFaninNum( Gia_Man_t * p, Gia_Obj_t * pObj )  { if ( Gia_ObjIsMux(p, pObj) ) return 3; if ( Gia_ObjIsAnd(pObj) ) return 2; if ( Gia_ObjIsCo(pObj) ) return 1; return 0; }
// Reverse lookup: which fanin slot of pObj holds pFanin.  Its callers walk a
// fanout list and need the position the current object occupies in each fanout
// (giaFanout.c:L130, L169; giaMuxes.c:L975).  It compares plain addresses, so
// pFanin is a regular address and not a tagged one, and it reaches the third
// slot through Gia_ObjFanin2, which maps the stored 0 of a non-MUX object to
// object 0 - so a search for the constant can be answered with slot 2.  A fanin
// that is not found reaches assert(0), and returns -1 with assertions disabled.
static inline int          Gia_ObjWhatFanin( Gia_Man_t * p, Gia_Obj_t * pObj, Gia_Obj_t * pFanin )  { if ( Gia_ObjFanin0(pObj) == pFanin ) return 0; if ( Gia_ObjFanin1(pObj) == pFanin ) return 1; if ( Gia_ObjFanin2(p, pObj) == pFanin ) return 2; assert(0); return -1; }

// COMBINATIONAL-OUTPUT DRIVERS.  A combinational output stores one offset that
// is used, so "the driver" is unambiguous: it is fanin 0.  These four take a
// position in the output order rather than an object identifier, resolve it
// through Gia_ManCo or Gia_ManPo, and then read fanin 0 of the object found.
// The three constant tests differ in what they compare.  Gia_ManPoIsConst
// compares the driver's identifier against 0, so it answers yes for either
// constant polarity.  Gia_ManPoIsConst0 and Gia_ManPoIsConst1 compare the
// driver's literal, so each answers yes for one polarity only.
static inline int          Gia_ManCoDriverId( Gia_Man_t * p, int iCoIndex )    { return Gia_ObjFaninId0p(p, Gia_ManCo(p, iCoIndex));                        }
static inline int          Gia_ManPoIsConst( Gia_Man_t * p, int iPoIndex )     { return Gia_ObjFaninId0p(p, Gia_ManPo(p, iPoIndex)) == 0;                   }
static inline int          Gia_ManPoIsConst0( Gia_Man_t * p, int iPoIndex )    { return Gia_ManIsConst0Lit( Gia_ObjFaninLit0p(p, Gia_ManPo(p, iPoIndex)) ); }
static inline int          Gia_ManPoIsConst1( Gia_Man_t * p, int iPoIndex )    { return Gia_ManIsConst1Lit( Gia_ObjFaninLit0p(p, Gia_ManPo(p, iPoIndex)) ); }

// COPY MAPS.  A rebuild reads one manager and writes another, and needs a map
// from each source object to the literal that now represents it in the new
// manager.  Four storages carry such a map, each with its own index arithmetic
// and its own miss marker:
//   (1) Value in the source object, read below and by the Fanin*Copy helpers.
//       Gia_ManFillValue sets every Value to ~0, which is the miss marker;
//       Gia_ManCleanValue sets every Value to 0, a valid literal that no test
//       can distinguish from a copy (giaUtil.c:L420-454).
//   (2) &p->vCopies indexed as Gia_ManObjNum(p) * f + ObjId, one plane per
//       time frame, through Gia_ObjCopyF.
//   (3) &p->vCopies indexed as ObjId, through Gia_ObjCopyArray, miss -1.
//   (4) &p->vCopies2 indexed as ObjId, through Gia_ObjCopy2Array, miss -1.
// (2) and (3) are two index conventions over one vector, so they are
// alternatives a pass chooses between rather than maps it can hold at once.
// Gia_ObjCopy treats Value as a literal and returns the address of the object
// it names, discarding the inversion; Gia_ObjLitCopy keeps the inversion by
// re-applying the argument literal's own complement to the stored one.
static inline Gia_Obj_t *  Gia_ObjCopy( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ManObj( p, Abc_Lit2Var(pObj->Value) );                              }
static inline int          Gia_ObjLitCopy( Gia_Man_t * p, int iLit )           { return Abc_LitNotCond( Gia_ManObj(p, Abc_Lit2Var(iLit))->Value, Abc_LitIsCompl(iLit));     }

// Fanin-and-copy in one step, on mechanism (1): resolve a fanin through its
// offset, read the literal that fanin was copied to, and re-apply the edge's
// complement bit.  This is the form a rebuild loop passes straight to an
// append or hashing entry point.  Gia_ObjFanin2Copy dereferences whatever
// Gia_ObjFanin2 returns, and that is a null address on a manager without
// p->pMuxes; its callers reach it from a real-MUX branch, where the side array
// exists and the third fanin is a stored literal (giaDup.c:L1567).
static inline int          Gia_ObjFanin0Copy( Gia_Obj_t * pObj )               { return Abc_LitNotCond( Gia_ObjFanin0(pObj)->Value, Gia_ObjFaninC0(pObj) );     }
static inline int          Gia_ObjFanin1Copy( Gia_Obj_t * pObj )               { return Abc_LitNotCond( Gia_ObjFanin1(pObj)->Value, Gia_ObjFaninC1(pObj) );     }
static inline int          Gia_ObjFanin2Copy( Gia_Man_t * p, Gia_Obj_t * pObj ){ return Abc_LitNotCond(Gia_ObjFanin2(p, pObj)->Value, Gia_ObjFaninC2(p, pObj)); }

// Mechanism (2), frame-indexed: entry Gia_ManObjNum(p) * f + ObjId of
// &p->vCopies, one contiguous plane of Gia_ManObjNum(p) entries per time frame
// f.  Unrolling a sequential graph needs one map per frame, which is what the
// multiplication buys.  The vector is sized by the caller; there is no clean
// helper for this layout, and the plane arithmetic assumes the object count
// does not change while the map is in use.
static inline int          Gia_ObjCopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj )               { return Vec_IntEntry(&p->vCopies, Gia_ManObjNum(p) * f + Gia_ObjId(p,pObj));      }
static inline void         Gia_ObjSetCopyF( Gia_Man_t * p, int f, Gia_Obj_t * pObj, int iLit )  { Vec_IntWriteEntry(&p->vCopies, Gia_ManObjNum(p) * f + Gia_ObjId(p,pObj), iLit);  }
// Mechanism (3), flat over the same vector: entry ObjId of &p->vCopies, with
// no frame multiplier.  Gia_ManCleanCopyArray sizes it to the object count and
// fills it with -1, which is the miss marker for this layout and for
// mechanism (4) - a different marker from the ~0 that mechanism (1) uses.
static inline int          Gia_ObjCopyArray( Gia_Man_t * p, int iObj )                          { return Vec_IntEntry(&p->vCopies, iObj);                                          }
static inline void         Gia_ObjSetCopyArray( Gia_Man_t * p, int iObj, int iLit )             { Vec_IntWriteEntry(&p->vCopies, iObj, iLit);                                      }
static inline void         Gia_ManCleanCopyArray( Gia_Man_t * p )                               { Vec_IntFill( &p->vCopies, Gia_ManObjNum(p), -1 );                                }

// Mechanism (4), flat over a second vector, &p->vCopies2.  Having two flat
// maps lets one pass hold two independent object-to-literal mappings at the
// same time, which a single vector cannot express.
static inline int          Gia_ObjCopy2Array( Gia_Man_t * p, int iObj )                          { return Vec_IntEntry(&p->vCopies2, iObj);                                          }
static inline void         Gia_ObjSetCopy2Array( Gia_Man_t * p, int iObj, int iLit )             { Vec_IntWriteEntry(&p->vCopies2, iObj, iLit);                                      }
static inline void         Gia_ManCleanCopy2Array( Gia_Man_t * p )                               { Vec_IntFill( &p->vCopies2, Gia_ManObjNum(p), -1 );                                }

// The fanin-and-copy forms for mechanisms (2) and (3), matching
// Gia_ObjFanin0Copy above but reading the vector-backed maps.  The CopyArray
// pair resolves the fanin in the identifier domain through Gia_ObjFaninId0p
// rather than in the address domain, since the map is indexed by identifier.
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

// TRAVERSAL IDENTIFIERS.  A depth-first pass needs a visited mark per object,
// and clearing a bit on every object before each pass costs a full sweep.  The
// alternative used here is a generation counter: p->pTravIds holds one int per
// object and p->nTravIds holds the current generation, so an object counts as
// marked when its entry equals p->nTravIds.  Advancing the generation
// invalidates every mark at once, which makes the clear O(1) instead of
// O(nObjs).  Gia_ManIncrementTravId does that advance, and also allocates
// p->pTravIds lazily at Gia_ManObjNum(p) + 100 entries, doubles and zeroes the
// new half when the object count outgrows it (giaUtil.c:L215-230).
// Two generations are reachable through these accessors.  The Previous forms
// write and test p->nTravIds - 1, so a pass can advance the counter and still
// see which objects the immediately preceding pass touched; none of the twelve
// asks about a third, so nothing older than the previous generation is
// distinguishable through this interface.  The storage is not so limited: an
// entry is a plain int holding whatever generation last wrote it, and a pass
// that indexes p->pTravIds directly can span more generations than these
// accessors expose (giaUnate.c:L124-136 works over three).
// Every accessor asserts that the identifier is below p->nTravIdsAlloc.  That
// upper bound is the only guard: a negative identifier is not rejected, and
// there is no lazy allocation on this path, so the side array has to have been
// created by an earlier Gia_ManIncrementTravId.
// The Update forms are test-and-set: they return 1 if the object was already
// marked in that generation and 0 after marking it, which is the shape a
// recursive walk uses to stop at an object it has already entered.
// The Id forms take an identifier instead of an address and skip the Gia_ObjId
// subtraction.  An identifier keeps naming the same position when an append
// moves p->pObjs, which an address does not; that on its own does not make these
// accessors safe across an append, since p->pTravIds grows only inside
// Gia_ManIncrementTravId and an object appended past p->nTravIdsAlloc trips the
// assertion until the generation is next advanced.
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
// OBJECT APPEND.  Every constructor in the block that follows obtains its
// storage here, and this is the only function in the header that grows the
// object array.  What it does, in order:
//   - Growth is by doubling, clamped at the ceiling: nObjNew is
//     min(2 * nObjsAlloc, 2^29).  The clamp is why the ceiling is reached
//     exactly rather than overshot.
//   - Still on that branch, when p->nObjs has already reached 2^29 the function
//     prints "Hard limit on the number of nodes (2^29) is reached. Quitting..."
//     and calls exit(1).  Both the clamp and that test live inside the capacity
//     branch, so 2^29 is the ceiling this helper enforces as it grows and not a
//     bound checked anywhere else: Gia_ManStart asserts only nObjsMax > 0
//     (giaMan.c:L71), so a manager given a larger initial capacity never
//     reaches the test.  Where it does fire, the ceiling is enforced by ending
//     the process, so no constructor in this header returns an out-of-space code.
//   - p->pObjs is reallocated and the grown tail is zeroed, so a freshly
//     appended object reads as all-zero in all three words before its
//     constructor writes it.
//   - p->pMuxes, when the manager carries it, is reallocated and zeroed in
//     lockstep, which keeps the side array indexable by object identifier.
//   - When the hash table is live, one entry is pushed onto p->vHash so that
//     vector keeps exactly one slot per object, which is the layout
//     Gia_ManHashFind walks (giaHash.c:L83-97).
//   - The new object's address is returned and p->nObjs is advanced.
// Two invalidation hazards arise here, on two different arrays, and both are
// conditional.  The growth branch runs only when p->nObjs has reached
// p->nObjsAlloc, and its ABC_REALLOC of p->pObjs may move the array, in which
// case a Gia_Obj_t address held across the call stops naming its object;
// storing fanins as offsets rather than as addresses is what keeps the objects
// themselves correct across such a move.  Separately, the Vec_IntPush onto
// p->vHash may move that vector, in which case an interior int * into it taken
// before the call is stale.  The hashing layer writes through such a pointer
// directly while the vector still has spare capacity, and only on the
// capacity-exhausting path appends first and re-runs Gia_ManHashFind for a
// fresh position (giaHash.c:L592-600, L669-677, L749-757).
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
// THE APPEND FAMILY.  Seventeen definitions carry the Gia_ManAppend prefix: the
// growth helper above and sixteen constructors.  Only six of the sixteen write
// an object directly - Ci, And, XorReal, MuxReal, Buf and Co - and those six are
// where the encoding is actually laid down.  The other ten are compositions:
// they call the direct six one or more times and combine the returned literals.
// Two distinctions run through the family and are easy to conflate.
//   Real versus structural.  A "real" XOR or MUX is one object, created by
//   Gia_ManAppendXorReal or Gia_ManAppendMuxReal.  A real XOR is recognised
//   afterwards from its offset ordering, iDiff0 < iDiff1.  A real MUX is not:
//   it is AND-shaped, iDiff0 > iDiff1, and separating it from a plain AND needs
//   a positive entry for the object in p->pMuxes.  A "structural" XOR or MUX is
//   a cluster of AND objects created by Gia_ManAppendXor or Gia_ManAppendMux,
//   and nothing marks it as having been a XOR or a MUX once it is built.
//   Folding versus not folding.  The plain constructors create an object
//   unconditionally.  Only the variants whose names end in 2 test their
//   arguments for constants and duplicates first and may return an existing
//   literal without creating anything.
// Return convention: the six direct constructors return Gia_ObjId(p, pObj) << 1,
// an uncomplemented literal naming the object just created.  The compositions
// return whatever their combination produces, which can be complemented and,
// for the folding variants, can be the constant literal 0 or 1.
//
// Combinational input.  It has no fanin, so iDiff0 is set to the GIA_NONE
// sentinel; iDiff1 is overloaded to hold the input's position in the
// combinational-input order, taken from the current size of p->vCis before the
// identifier is pushed onto that list; and fTerm marks the object a terminal so
// that Gia_ObjIsCi reads it correctly.
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

// AND.  The two extern declarations immediately above are placed here because
// the body below calls them from inside the header.
// Both fanins are stored, and the branch decides which one lands in iDiff0.
// The comparison is between the two *literals*, so the complement bits take
// part in it, and the outcome is that the object always ends up with
// iDiff0 >= iDiff1: the smaller literal names the earlier variable, hence the
// larger offset, and it is written to iDiff0 in the first branch and reached by
// the swap in the second.  That ordering is what later distinguishes an AND
// from a real XOR, which orders its offsets the other way round.
// The third assertion requires the two fanins to be different objects unless
// p->fGiaSimple is set.  With that flag set the assertion is relaxed, and
// AND(x, x) then produces two equal offsets, which is the pattern Gia_ObjIsBuf
// reads as a buffer.
// No constant folding happens here.  An argument literal of 0 or 1 is stored as
// an ordinary fanin on object 0; the folding lives in Gia_ManAppendAnd2 and in
// the hashing layer, and it is what normally keeps the distinct-fanin
// assertion satisfiable.
// Four optional manager members add side effects to this one constructor - two
// are pointers whose presence is the condition, two are flags:
//   p->pFanData  - the new object is linked into the dynamic fanout lists of
//                  both of its fanins.
//   p->fSweeper  - fMark0 and fMark1 of each fanin serve as a two-bit
//                  saturating fanout counter (first fanout sets fMark0, any
//                  later one sets fMark1), and fPhase of the new object is
//                  recomputed as the conjunction of its fanins' phases.
//   p->fBuiltInSim - fPhase is recomputed the same way and
//                  Gia_ManBuiltInSimPerform is run for the new object.
//   p->vSuppWords - Gia_ManQuantSetSuppAnd computes the new object's support.
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
// Real XOR - one object, not a cluster.  The branch here compares *variables*
// rather than literals, and it orders them the opposite way from
// Gia_ManAppendAnd, so the object always ends up with iDiff0 < iDiff1.  That
// strict inequality is the whole type tag: it cannot arise from
// Gia_ManAppendAnd, which always leaves iDiff0 >= iDiff1, so Gia_ObjIsXor needs
// nothing but the object's own two offsets.
// Comparing variables and not literals matters because a XOR absorbs
// complements: inverting either input inverts the output, so the two
// complement bits cannot be allowed to influence which offset is larger.
// p->nXors counts these objects, and the counter is what Gia_ManXorNum reports.
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
// Real MUX - also one object, and this is the case the offsets cannot express.
// The branch compares variables and orders them so that iDiff0 > iDiff1, which
// satisfies the AND condition iDiff0 >= iDiff1: the three words of a real MUX
// are indistinguishable from those of a plain AND.  What separates them is
// external - the control input is stored as a literal in p->pMuxes at the
// object's own index, and Gia_ObjIsMuxId tests that the stored entry is
// *positive*, since 0 is the entry every non-MUX object carries.  The first
// assertion therefore requires the side array to exist before a real MUX can be
// created at all.  Because 0 doubles as the non-MUX marker, a control literal of
// 0 stored through the first branch leaves an object that Gia_ObjIsMux does not
// report as a MUX; the hashing layer never presents that case, folding a control
// literal below 2 away before it calls here (giaHash.c:L642-643).
// When the data inputs are swapped into the other order the stored control
// literal is complemented, because exchanging the two data inputs of a
// multiplexer and inverting its control select the same function.
// The last assertion holds only while the hash table is live, and requires the
// first data literal to be uncomplemented - the canonical form the hashing
// layer relies on.  p->nMuxes counts these objects.
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
// Buffer.  One fanin written into both slots: iDiff0 == iDiff1 and
// fCompl0 == fCompl1.  Equal offsets are outside the range Gia_ManAppendAnd
// produces for two distinct fanins and outside the range Gia_ManAppendXorReal
// produces at all, which is what makes equality usable as the buffer tag.  The
// object is not a terminal, so Gia_ObjIsAnd answers yes for it and every
// dispatch over object kinds has to test for a buffer before testing for an
// AND.  p->nBufs counts these objects, and Gia_ManForEachBuf iterates nothing
// when that counter is zero.
static inline int Gia_ManAppendBuf( Gia_Man_t * p, int iLit )  
{ 
    Gia_Obj_t * pObj = Gia_ManAppendObj( p );
    assert( iLit >= 0 && Abc_Lit2Var(iLit) < Gia_ManObjNum(p) );
    pObj->iDiff0  = pObj->iDiff1  = Gia_ObjId(p, pObj) - Abc_Lit2Var(iLit);
    pObj->fCompl0 = pObj->fCompl1 = Abc_LitIsCompl(iLit);
    p->nBufs++;
    return Gia_ObjId( p, pObj ) << 1;
}
// Combinational output.  One fanin, so only iDiff0 and fCompl0 describe an
// edge; iDiff1 is overloaded to hold the output's position in the
// combinational-output order, taken from the current size of p->vCos; and
// fTerm marks the object a terminal.  Nothing distinguishes a combinational
// input from a combinational output except iDiff0: GIA_NONE for the input, a
// real offset for the output.
// The second assertion refuses a driver that is itself a combinational output,
// so outputs cannot be chained.  Note that the object is appended after the
// assertions, so the driver's identifier is compared against an object count
// that does not yet include this object.
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
// Structural composites, built on Gia_ManAppendAnd and therefore not folding.
// Gia_ManAppendOr creates one AND object over the two inverted inputs and
// inverts the result, so an OR costs a single object and no OR encoding exists.
// Gia_ManAppendMux creates three AND objects, Gia_ManAppendMaj four, and
// Gia_ManAppendXor delegates to Gia_ManAppendMux and so creates three.  None of
// these leaves any record that the cluster came from a MUX or a XOR; the
// single-object forms above are the ones that do.
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

// The folding variants.  This paragraph covers the structural family from
// Gia_ManAppendAnd2 down to Gia_ManAppendXor2.  Gia_ManAppendXorReal2 further
// below folds as well, but its callee keeps an assertion that these do not, so
// it is described separately.  Each of the variants here tests its arguments
// before creating anything: a constant argument, two identical arguments, or two
// complementary arguments are answered with an existing literal, and only a
// genuinely new pair reaches Gia_ManAppendAnd.  Literals 0 and 1 name object 0
// with the two polarities, which is why the constant tests read "iLit < 2".
// The folding is what normally guarantees the distinct-fanin assertion inside
// Gia_ManAppendAnd, since the equal and complementary cases never get through.
// Under p->fGiaSimple the whole test block is skipped, so these variants become
// pass-throughs; that same flag relaxes the distinct-fanin assertion, so the
// equal and complementary cases are then stored as ordinary objects.  Or2, Mux2,
// Maj2 and Xor2 mirror their non-folding counterparts and differ only in calling
// Gia_ManAppendAnd2 underneath.
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

// The folding entry point for the single-object XOR.  Its constant cases differ
// from the AND ones because XOR is not absorbing: a constant input passes the
// other input through, inverted or not, identical inputs give 0 and
// complementary inputs give 1.  p->fGiaSimple skips the tests here too and sends
// everything to Gia_ManAppendXorReal, whose distinct-variable assertion is
// unconditional - so an equal or complementary pair then reaches that assertion
// rather than being stored.  That is where this variant parts company with the
// structural ones above, which p->fGiaSimple turns into true pass-throughs.
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

// The one in-place edit of a stored offset in this header.  It rewrites iDiff0
// and fCompl0 of an existing combinational output so that the output is driven
// by a different literal.  iDiff1 is left alone, so the output keeps its
// position in the combinational-output order.  The assertion requires the
// output's identifier to be strictly greater than the new driver's variable,
// which keeps the offset positive and so keeps the backward-reference property
// intact.  Dynamic fanout data, where present, is not updated here.
static inline void Gia_ManPatchCoDriver( Gia_Man_t * p, int iCoIndex, int iLit0 )  
{
    Gia_Obj_t * pObjCo  = Gia_ManCo( p, iCoIndex );
    assert( Gia_ObjId(p, pObjCo) > Abc_Lit2Var(iLit0) );
    pObjCo->iDiff0  = Gia_ObjId(p, pObjCo) - Abc_Lit2Var(iLit0);
    pObjCo->fCompl0 = Abc_LitIsCompl(iLit0);
}

// TERNARY SIMULATION.  Two unrelated representations of a three-valued logic
// live in this region, and they do not share a storage format.
// The three codes below are plain int values used by Gia_XsimNotCond and
// Gia_XsimAndCond, which take and return values and touch no object.  The codes
// are chosen so that GIA_ZER + fCompl selects between 0 and 1 by arithmetic,
// which is what lets those two helpers apply an edge's complement bit without a
// branch; GIA_UND absorbs, so an undefined input yields an undefined result
// unless the other input is controlling.
// The Gia_ObjTerSim accessors that follow store a state *inside the object*,
// packed into the fMark0 and fMark1 pair, and the pair encodes four states, not
// three: (0,0) is C, (1,0) is 0, (0,1) is 1 and (1,1) is X.  C has no counterpart
// among the three int codes; it is the all-zero pattern a freshly appended object
// already carries, so it reads as "no value assigned" rather than as a logic
// value.  The other three line up with GIA_ZER, GIA_ONE and GIA_UND in meaning
// while sharing none of their bit patterns.  This is a second and incompatible
// meaning for those two bits - a pass running ternary
// simulation owns both of them for its whole duration and cannot also use them
// as general-purpose marks, and neither can anything that runs while
// p->fSweeper is set, since that flag claims the same pair as a fanout counter.
// The Fanin variants combine a fanin's packed state with that edge's complement
// bit, which is the same job Gia_XsimNotCond does for the int form.
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

// EQUIVALENCE CLASSES AND CHOICES.  Two parallel side arrays, both indexed by
// object identifier, hold a partition of the objects into classes of objects
// believed or proved functionally equivalent: p->pReprs gives each object its
// class representative, and p->pNexts threads the members of one class into a
// singly linked chain.  Neither lives in the object, so a class assignment
// costs nothing in the twelve bytes.
// These helpers key on GIA_VOID, not on GIA_NONE.  GIA_VOID is 2^28-1, the
// maximum of the 28-bit iRepr field of Gia_Rpr_t; GIA_NONE is 2^29-1 and belongs
// to the 29-bit offset fields.  The two are used through the API of their own
// domain: iRepr is set and tested by the accessors below, and the offset fields
// by the fanin readers and the kind predicates.  How a crossed assignment
// behaves is set out at the sentinel definitions near the top of this file.
// Class membership is read from the pair of arrays rather than from a flag:
// a head has no representative but does have a next; a tail has a
// representative but no next; "none" has neither; and a representative of 0
// means the object was found equivalent to the constant.  A chain end is
// signalled by 0 and not by GIA_VOID, since object 0 is the constant and can
// never be a chain member.
// Gia_ObjSetRepr asserts the representative precedes the object and
// Gia_ObjSetReprRev asserts it follows, so the two setters serve forward and
// backward sweeps and are not interchangeable either.
// p->pSibls is a third, separate array used for choice nodes; Gia_ObjSibl
// returns 0 both when the array is absent and when an object has no sibling.
// The remaining accessors below read the other bit fields of Gia_Rpr_t -
// fProved, fFailed and the two colour bits - which is why that struct is one
// word wide rather than a bare integer.
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

// ITERATORS, first group.  Every iterator in this header is a for loop that a
// user block follows, and the filtering ones end in "if ( !predicate ) {} else"
// so that the user block becomes the else branch and runs only for objects the
// predicate accepts.  That trailer is also what makes the macros safe to use
// with a following break or continue, and it is why they are written without a
// closing brace.
// The class iterators below walk identifiers rather than addresses.
// Gia_ManForEachClass starts at 1 and Gia_ManForEachClass0 starts at 0, so only
// the second one can report the constant's class.  Gia_ManForEachClassReverse
// stops at 1, so it never visits object 0.  The three Gia_ClassForEachObj forms
// walk one chain: the plain form starts at the head, the 1 form skips the head
// and visits only the other members, and the Start form resumes from a given
// member.  Each of them asserts that the object it was handed is a class head.
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


// STATIC FANOUT.  Fanout is not stored in the object either - the encoding
// records only the direction from an object to its fanins - so a pass that needs
// fanout builds a side table.  p->vFanout is self-indexing: its first
// Gia_ManObjNum(p) entries are offsets into the same vector, and the fanout
// identifiers of an object live in the run that begins at its own offset.
// Gia_ObjFoffsetId reads the offset, Gia_ObjFanoutId reads offset + i, and
// p->vFanoutNums holds each run's length.  One vector therefore carries both
// the directory and the data.
// "Static" distinguishes this table from the dynamic fanout lists behind
// p->pFanData, which Gia_ManAppendAnd maintains incrementally; the static table
// is built once for a fixed graph and is not updated by an append.
static inline int         Gia_ObjFoffsetId( Gia_Man_t * p, int Id )                { return Vec_IntEntry( p->vFanout, Id );                                 }
static inline int         Gia_ObjFoffset( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ObjFoffsetId( p, Gia_ObjId(p, pObj) );                      }
static inline int         Gia_ObjFanoutNumId( Gia_Man_t * p, int Id )              { return Vec_IntEntry( p->vFanoutNums, Id );                             }
static inline int         Gia_ObjFanoutNum( Gia_Man_t * p, Gia_Obj_t * pObj )      { return Gia_ObjFanoutNumId( p, Gia_ObjId(p, pObj) );                    }
static inline int         Gia_ObjFanoutId( Gia_Man_t * p, int Id, int i )          { return Vec_IntEntry( p->vFanout, Gia_ObjFoffsetId(p, Id) + i );        }
static inline Gia_Obj_t * Gia_ObjFanout0( Gia_Man_t * p, Gia_Obj_t * pObj )        { return Gia_ManObj( p, Gia_ObjFanoutId(p, Gia_ObjId(p, pObj), 0) );     }
static inline Gia_Obj_t * Gia_ObjFanout( Gia_Man_t * p, Gia_Obj_t * pObj, int i )  { return Gia_ManObj( p, Gia_ObjFanoutId(p, Gia_ObjId(p, pObj), i) );     }
static inline void        Gia_ObjSetFanout( Gia_Man_t * p, Gia_Obj_t * pObj, int i, Gia_Obj_t * pFan )   { Vec_IntWriteEntry( p->vFanout, Gia_ObjFoffset(p, pObj) + i, Gia_ObjId(p, pFan) ); }
static inline void        Gia_ObjSetFanoutInt( Gia_Man_t * p, Gia_Obj_t * pObj, int i, int x )           { Vec_IntWriteEntry( p->vFanout, Gia_ObjFoffset(p, pObj) + i, x );                  }

// The first two forms iterate a run in the address and identifier domains.  The
// third, Gia_ObjForEachFanoutStaticIndex, carries the computed index as a
// conjunct of its loop condition - "(Index = Vec_IntEntry(p->vFanout, Id)+i)" -
// so the loop ends as soon as that expression evaluates to 0.
#define Gia_ObjForEachFanoutStatic( p, pObj, pFanout, i )         \
    for ( i = 0; (i < Gia_ObjFanoutNum(p, pObj)) && (((pFanout) = Gia_ObjFanout(p, pObj, i)), 1); i++ )
#define Gia_ObjForEachFanoutStaticId( p, Id, FanId, i )           \
    for ( i = 0; (i < Gia_ObjFanoutNumId(p, Id)) && ((FanId = Gia_ObjFanoutId(p, Id, i)), 1); i++ )
#define Gia_ObjForEachFanoutStaticIndex( p, Id, FanId, i, Index ) \
    for ( i = 0; (i < Gia_ObjFanoutNumId(p, Id)) && (Index = Vec_IntEntry(p->vFanout, Id)+i) && ((FanId = Vec_IntEntry(p->vFanout, Index)), 1); i++ )

// MAPPING.  Three mapping representations are declared, each with its own
// manager field, its own Gia_ManHas... test that reports whether that field is
// present, and its own accessor set keyed to that field's layout.
//   p->vMapping - one flat Vec_Int_t, self-indexing in the same style as
//     p->vFanout: entry Id is either 0, meaning the object is not a LUT root, or
//     an offset into the same vector where the size is stored followed by the
//     fanin identifiers.  Gia_ObjLutFanins returns a raw int * into the vector,
//     so it is invalidated by anything that resizes the vector.  One slot past
//     the fanins holds a MUX identifier, and Gia_ObjLutIsMux reads a negative
//     value there as the "this LUT is a MUX" case.
//   p->vMapping2 - a Vec_Wec_t, one Vec_Int_t of fanins per object, which trades
//     the offset arithmetic for one vector header per object and is paired with
//     p->vFanouts2 for the reverse direction.
//   p->vCellMapping - the same flat self-indexing layout as p->vMapping but
//     indexed by *literal* rather than by object, which is why
//     Gia_ManForEachCell runs from 2 to 2*Gia_ManObjNum(p).  Entries -1 and -2
//     encode an inverter and a buffer instead of an offset.
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

// The mapping iterators, one family per representation.  The forms that scan the
// object range start at 1 and the Reverse ones stop at 1, so neither visits
// object 0.  The Vec forms are driven by a caller-supplied list of identifiers
// instead and range over that list.  Gia_ManForEachCell runs over literals for
// the reason given above.  The Index forms expose the vector position of each
// fanin alongside its value.
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

// ITERATORS, main family.  Same shape as the first group: a for loop the user
// block follows, with "if ( !predicate ) {} else" appended on the filtering
// forms so the block runs only for accepted objects.  Reading the loop header is
// the reliable way to know what a given macro visits, since the names encode
// only part of it.  Four axes recur across the family.
//   Address or identifier.  A plain form assigns a Gia_Obj_t * each step; an Id
//   form assigns only an identifier.  An identifier already obtained keeps
//   naming the same array position if an append relocates p->pObjs, whereas an
//   address does not; that does not make the Id forms safe to append inside.
//   Appending during a walk moves p->nObjs, which is the loop bound of the forms
//   that walk the object array; it pushes onto p->vCis or p->vCos, which is the
//   bound of the forms that walk those lists; and it extends the side arrays that
//   the body may be reading.  Growing what a loop is walking is a separate
//   concern from the address-versus-identifier choice.
//   Where the walk starts.  A trailing 1 means the loop starts at object 1 and
//   skips the constant.
//   What drives it.  Some forms walk the object array directly, some walk
//   p->vCis or p->vCos, and the Vec forms walk a caller-supplied Vec_Int_t of
//   identifiers - or, for Gia_ManForEachObjVecLit, of literals, exposing the
//   complement bit as a separate loop variable.
//   Direction.  Reverse forms count down and are used where a pass must see
//   an object after all of its fanouts.
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
// The two reverse forms over the object array differ only in their bound, and
// the difference is object 0: Gia_ManForEachObjReverse uses i >= 0 and does
// visit the constant, while Gia_ManForEachObjReverse1 uses i > 0 and stops
// before it.  Gia_ManForEachAndReverse and Gia_ManForEachAndReverseId below use
// i > 0 as well, so they skip it too - which is harmless there because the
// constant is not an AND.
#define Gia_ManForEachObjReverse( p, pObj, i )                          \
    for ( i = p->nObjs - 1; (i >= 0) && ((pObj) = Gia_ManObj(p, i)); i-- )
#define Gia_ManForEachObjReverse1( p, pObj, i )                         \
    for ( i = p->nObjs - 1; (i > 0) && ((pObj) = Gia_ManObj(p, i)); i-- )
// Gia_ManForEachBuf self-disables: its initialiser is
// "i = Gia_ManBufNum(p) ? 0 : p->nObjs", so on a manager with no buffers the
// index starts past the end and the loop body never runs, without a scan.
// Gia_ManForEachBufId has no such guard and always scans every object.
#define Gia_ManForEachBuf( p, pObj, i )                                 \
    for ( i = Gia_ManBufNum(p) ? 0 : p->nObjs; (i < p->nObjs) && ((pObj) = Gia_ManObj(p, i)); i++ )      if ( !Gia_ObjIsBuf(pObj) ) {} else
#define Gia_ManForEachBufId( p, i )                                     \
    for ( i = 0; (i < p->nObjs); i++ )                                     if ( !Gia_ObjIsBuf(Gia_ManObj(p, i)) ) {} else
// The predicate-filtered forms inherit exactly what their predicate accepts, and
// Gia_ObjIsAnd accepts more than plain ANDs: buffers, real XORs and real MUXes
// all satisfy it.  A loop that must treat those kinds differently tests for them
// inside the body.  Gia_ManForEachMux and Gia_ManForEachMuxId filter on
// Gia_ObjIsMuxId, which reads p->pMuxes and so visits nothing on a manager that
// has no side array.  Gia_ManForEachCand filters on Gia_ObjIsAnd or
// Gia_ObjIsCi, so it visits internal nodes and combinational inputs but neither
// the constant nor any combinational output.
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
// The terminal iterators walk the index lists p->vCis and p->vCos rather than
// the object array, so they visit terminals in creation order and skip every
// internal object without testing it.  The CoDriver forms take one further step
// and hand back each output's fanin instead of the output object itself.
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
// Positional subranges of the same two lists: primary inputs are the first
// Gia_ManPiNum(p) entries of p->vCis and flop outputs are the rest, primary
// outputs are the first Gia_ManPoNum(p) entries of p->vCos and flop inputs are
// the rest.  Nothing in an object records which of the two roles it has, so
// these iterators are correct only for a manager built in that order.
// Gia_ManForEachRiRo advances both lists together, which pairs each flop input
// with the flop output of the same register.
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

// The box-aware family.  On a manager whose terminals include box boundaries,
// the plain iterators would visit the box terminals as ordinary combinational
// inputs and outputs.  These four instead take their bounds from the four
// boundary indices in the manager - iFirstAndObj, iFirstPoObj, iFirstNonPiId and
// iFirstPoId - so they cover only the logic between the boundaries, or only the
// true primary terminals.  Those four fields are set together by the pass that
// establishes the boundaries, and the iterators read them without checking that
// they have been set.  On a manager where all four are still zero the first
// three run empty, because each compares against a zero upper bound, while
// Gia_ManForEachCoWithBoxes degenerates into a walk over every combinational
// output.
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

